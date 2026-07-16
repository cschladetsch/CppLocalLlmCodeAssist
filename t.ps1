<#
.SYNOPSIS
    Single entry point for CppCoder: init submodules, configure, build,
    test, and optionally run a research question or open the web UI.

.PARAMETER Clean
    Remove the build/ directory first.

.PARAMETER SkipBuild
    Skip configure/build entirely (implies -SkipTests unless tests were
    already built). Useful with -Question or -OpenWeb only.

.PARAMETER SkipTests
    Skip running ctest after building.

.PARAMETER Jobs
    Parallel build jobs. Defaults to the processor count.

.PARAMETER Compiler
    Which compiler/generator to configure with:
      auto      (default) clang-cl on Windows, whatever CMake finds by default
                elsewhere. Note: LLVM 21.1.1 has a real OOM bug parsing MSVC's
                C++23 STL headers on trivial files, confirmed on BOTH clang++ and
                clang-cl driver modes -- if you hit it, downgrade LLVM (18.1.8 is
                a safe bet: winget uninstall --id LLVM.LLVM; winget install --id
                LLVM.LLVM --version 18.1.8 -e) rather than switching -Compiler.
      clang-cl  Same as auto's Windows default, explicit. LLVM's MSVC-compatible
                driver -- still clang, still Ninja.
      clang     plain clang++/clang on PATH -- same LLVM 21.1.1 OOM risk as clang-cl
      msvc      Ninja + cl.exe instead of clang entirely. Auto-imports the VS
                developer environment (INCLUDE/LIB/cl.exe on PATH) if not already
                present -- no need to run from "Developer PowerShell for VS 2022".
      msvc-vs   Visual Studio generator (MSBuild) + cl.exe. Slower, multi-config,
                but doesn't need the dev environment imported. Used automatically
                as a fallback if -Compiler msvc can't find/import cl.exe.
      default   whatever CMake finds on PATH, no override

.PARAMETER Question
    If given, runs build/src/cppcoder against -Codebase with this question
    after building.

.PARAMETER Codebase
    Codebase root to research when -Question is given. Defaults to this
    repo's root.

.PARAMETER Model
    Ollama model tag passed to cppcoder. Defaults to qwen2.5-coder:7b.

.PARAMETER EventsFile
    Path to write JSON-Lines engine events when -Question is given.
    Defaults to build/last_run.jsonl.

.PARAMETER LogLevel
    Log level passed to cppcoder: trace|debug|info|warn|err|critical|off.

.PARAMETER OpenWeb
    Opens web/index.html in the default browser after everything else.

.PARAMETER Serve
    Starts the web chat UI (web/chat.html) instead of researching a
    question: builds (unless -SkipBuild), then runs
    `cppcoder --serve` and opens the chat page in your browser. Blocks
    until you Ctrl+C. Uses -Model as the default model and -ServeHost /
    -ServePort for the bind address.

.PARAMETER ServeHost
    Address to bind the chat server to when -Serve is given. Defaults to
    127.0.0.1.

.PARAMETER ServePort
    Port to bind the chat server to when -Serve is given. Defaults to 8765.

.EXAMPLE
    ./r.ps1 -Serve
    Build, then start the chat server + UI at http://127.0.0.1:8765 and
    open it in your browser. Requires Ollama running with at least one
    model pulled.

.EXAMPLE
    ./r.ps1
    Init submodules (if needed), configure with clang-cl on Windows
    (auto), build, run all 88 tests.

.EXAMPLE
    ./r.ps1 -Question "How does the judge prune directions?" -Codebase .
    Build, then run cppcoder with that question against this repo,
    writing events to build/last_run.jsonl.

.EXAMPLE
    ./r.ps1 -SkipBuild -OpenWeb
    Just open the task-graph UI (e.g. to load a previously recorded
    events file by hand) without touching the build.

.EXAMPLE
    ./r.ps1 -Clean -Jobs 8
    Full rebuild from scratch with 8 parallel jobs.

.EXAMPLE
    ./r.ps1 -Compiler msvc
    Switch off clang entirely and use cl.exe + Ninja instead.
#>

[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [int]$Jobs = 0,
    [ValidateSet('auto', 'msvc', 'msvc-vs', 'clang', 'clang-cl', 'default')]
    [string]$Compiler = 'auto',

    [string]$Question,
    [string]$Codebase = $PSScriptRoot,
    [string]$Model = 'qwen2.5-coder:7b',
    [string]$EventsFile,
    [ValidateSet('trace', 'debug', 'info', 'warn', 'err', 'critical', 'off')]
    [string]$LogLevel = 'info',

    [switch]$OpenWeb,

    [switch]$Serve,
    [string]$ServeHost = '127.0.0.1',
    [int]$ServePort = 8765
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# $IsWindows / $IsMacOS / $IsLinux are automatic variables that only
# exist in PowerShell 6+ (Core edition). Under Windows PowerShell 5.1
# (Desktop edition, e.g. plain 'powershell.exe') they don't exist and
# silently evaluate to $null rather than erroring, so every "if
# ($IsWindows)" below would quietly take the wrong branch. Desktop
# edition only ever runs on Windows, so that case is unambiguous.
if ($PSVersionTable.PSEdition -eq 'Core') {
    $OnWindows = $IsWindows
    $OnMacOS = $IsMacOS
}
else {
    $OnWindows = $true
    $OnMacOS = $false
}

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Test-CommandExists {
    param([string]$Name)
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Confirm-NinjaOrWarn {
    if (Test-CommandExists 'ninja') { return $true }
    Write-Warning ('ninja not found on PATH -- CMake will fall back to its own default ' +
                   'generator instead (often Visual Studio/.vcxproj on Windows, which is ' +
                   "slower). Install it (e.g. 'winget install Ninja-build.Ninja') for fast " +
                   'incremental builds.')
    return $false
}

function Import-VisualStudioEnvironment {
    # Locates the newest VS install with the C++ workload via vswhere,
    # runs its VsDevCmd.bat, and imports the resulting environment
    # (INCLUDE/LIB/PATH with cl.exe, etc.) into this PowerShell process --
    # equivalent to launching "Developer PowerShell for VS 2022"
    # yourself, but automatic. Returns $true if cl.exe ends up on PATH.
    if (-not $OnWindows) {
        return $false
    }

    $programFilesX86 = ${env:ProgramFiles(x86)}
    $programFiles = $env:ProgramFiles
    $vswhereCandidates = @()
    if ($programFilesX86) {
        $vswhereCandidates += (Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe')
    }
    if ($programFiles) {
        $vswhereCandidates += (Join-Path $programFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    }
    $vswhere = $vswhereCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $vswhere) {
        return $false
    }

    $vsInstallPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null
    if (-not $vsInstallPath) {
        return $false
    }

    $vsDevCmd = Join-Path $vsInstallPath 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path $vsDevCmd)) {
        return $false
    }

    Write-Step "Importing Visual Studio developer environment ($vsInstallPath)"
    $envDump = & cmd.exe /c "`"$vsDevCmd`" -arch=x64 -no_logo && set" 2>$null
    foreach ($line in $envDump) {
        if ($line -match '^([^=]+)=(.*)$') {
            [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }

    return (Test-CommandExists 'cl')
}

# ---------------------------------------------------------------------------
# 1. Submodules (external/CppLmmModelStore, external/spdlog, external/googletest)
# ---------------------------------------------------------------------------
if (-not $SkipBuild -and (Test-Path (Join-Path $PSScriptRoot '.gitmodules'))) {
    $submodulePaths = @('external/CppLmmModelStore', 'external/spdlog', 'external/googletest')
    $needsInit = $false
    foreach ($dir in $submodulePaths) {
        $marker = Join-Path (Join-Path $PSScriptRoot $dir) 'CMakeLists.txt'
        if (-not (Test-Path $marker)) {
            $needsInit = $true
        }
    }

    if ($needsInit) {
        if (-not (Test-CommandExists 'git')) {
            throw "git not found on PATH; cannot initialize submodules."
        }
        Write-Step 'Initializing git submodules'
        git submodule update --init --recursive
        if ($LASTEXITCODE -ne 0) { throw 'git submodule update failed' }
    }
}

# ---------------------------------------------------------------------------
# 2. Configure + build
# ---------------------------------------------------------------------------
$effectiveCompiler = $Compiler
if ($effectiveCompiler -eq 'auto') {
    # Stay on clang: plain clang++ targeting the MSVC ABI is what OOMs on
    # MSVC's C++23 STL headers. clang-cl (LLVM's MSVC-compatible driver)
    # is a different, unaffected code path, still clang, still Ninja.
    $effectiveCompiler = if ($OnWindows) { 'clang-cl' } else { 'default' }
}

# Only the Visual Studio generator (msvc-vs) is multi-config: binaries
# land under build/<target>/<Config>/ instead of build/<target>/, and
# both the build and ctest steps need an explicit -C/--config. Ninja
# (clang-cl, msvc, clang, default) is single-config.
$isMultiConfig = ($effectiveCompiler -eq 'msvc-vs')
$buildConfigArgs = @()
$ctestConfigArgs = @()
if ($isMultiConfig) {
    $buildConfigArgs = @('--config', 'Debug')
    $ctestConfigArgs = @('-C', 'Debug')
}

if (-not $SkipBuild) {
    if ($Clean -and (Test-Path 'build')) {
        Write-Step 'Removing existing build/'
        Remove-Item -Recurse -Force 'build'
    }

    if (-not (Test-CommandExists 'cmake')) {
        throw ("cmake not found on PATH. Install it first (e.g. 'sudo apt install cmake' " +
               "or 'winget install Kitware.CMake').")
    }

    $cmakeConfigureArgs = @('-B', 'build', '-S', '.')
    switch ($effectiveCompiler) {
        'clang-cl' {
            Write-Step ('Compiler: clang-cl. Note: LLVM 21.1.1 OOMs parsing MSVC C++23 STL ' +
                        'headers on both clang++ and clang-cl -- if that happens, downgrade ' +
                        'LLVM (winget install --id LLVM.LLVM --version 18.1.8 -e) rather than ' +
                        'switching -Compiler.')
            if (Confirm-NinjaOrWarn) { $cmakeConfigureArgs += @('-G', 'Ninja') }
            $cmakeConfigureArgs += @('-DCMAKE_CXX_COMPILER=clang-cl', '-DCMAKE_C_COMPILER=clang-cl')
        }
        'clang' {
            Write-Step ('Compiler: clang++. Note: LLVM 21.1.1 OOMs parsing MSVC C++23 STL ' +
                        'headers on both clang++ and clang-cl -- if that happens, downgrade ' +
                        'LLVM (winget install --id LLVM.LLVM --version 18.1.8 -e) rather than ' +
                        'switching -Compiler.')
            if (Confirm-NinjaOrWarn) { $cmakeConfigureArgs += @('-G', 'Ninja') }
            $cmakeConfigureArgs += @('-DCMAKE_CXX_COMPILER=clang++', '-DCMAKE_C_COMPILER=clang')
        }
        'msvc' {
            Write-Step 'Compiler: cl.exe + Ninja'
            $clAvailable = Test-CommandExists 'cl'
            if (-not $clAvailable) {
                $clAvailable = Import-VisualStudioEnvironment
            }
            if ($clAvailable -and (Confirm-NinjaOrWarn)) {
                $cmakeConfigureArgs += @('-G', 'Ninja', '-DCMAKE_CXX_COMPILER=cl', '-DCMAKE_C_COMPILER=cl')
            }
            else {
                if ($clAvailable) {
                    Write-Warning 'Falling back to -Compiler msvc-vs (Visual Studio generator) since ninja is unavailable.'
                }
                else {
                    Write-Warning ('cl.exe not found and the VS developer environment could not be ' +
                                   "auto-imported (no vswhere.exe / no matching VS install). Falling " +
                                   'back to -Compiler msvc-vs (Visual Studio generator).')
                }
                $effectiveCompiler = 'msvc-vs'
                $isMultiConfig = $true
                $buildConfigArgs = @('--config', 'Debug')
                $ctestConfigArgs = @('-C', 'Debug')
                $cmakeConfigureArgs += @('-G', 'Visual Studio 17 2022', '-A', 'x64')
            }
        }
        'msvc-vs' {
            Write-Step 'Compiler: cl.exe (Visual Studio generator)'
            $cmakeConfigureArgs += @('-G', 'Visual Studio 17 2022', '-A', 'x64')
        }
        default {
            # No override -- whatever CMake finds on PATH.
        }
    }

    # CMake refuses to reconfigure an existing build/ with a different
    # generator than it was created with ("Does not match the generator
    # used previously"). Detect that up front (now that $effectiveCompiler
    # is final -- the msvc->msvc-vs fallback above can change it) and wipe
    # build/ ourselves so switching -Compiler just works.
    $intendedGenerator = if ($effectiveCompiler -eq 'msvc-vs') { 'Visual Studio 17 2022' } else { $null }
    $cachePath = Join-Path 'build' 'CMakeCache.txt'
    if (Test-Path $cachePath) {
        $cachedGenerator = $null
        foreach ($line in Get-Content $cachePath) {
            if ($line -match '^CMAKE_GENERATOR:INTERNAL=(.*)$') {
                $cachedGenerator = $Matches[1]
                break
            }
        }
        $mismatch = $false
        if ($cachedGenerator) {
            if ($intendedGenerator -and $cachedGenerator -ne $intendedGenerator) { $mismatch = $true }
            if (-not $intendedGenerator -and $cachedGenerator -eq 'Visual Studio 17 2022') { $mismatch = $true }
        }
        if ($mismatch) {
            $shownIntended = if ($intendedGenerator) { $intendedGenerator } else { 'default' }
            Write-Step "Generator changed ($cachedGenerator -> $shownIntended); removing build/ to reconfigure cleanly"
            Remove-Item -Recurse -Force 'build'
        }
    }

    Write-Step "Configuring (cmake $($cmakeConfigureArgs -join ' '))"
    cmake @cmakeConfigureArgs
    if ($LASTEXITCODE -ne 0) {
        if ($effectiveCompiler -eq 'msvc-vs') {
            Write-Warning ("Configure failed -- if the 'Visual Studio 17 2022' generator wasn't " +
                           "found, install the 'Desktop development with C++' workload, or try " +
                           "-Compiler clang-cl instead.")
        }
        throw 'cmake configure failed'
    }

    if ($Jobs -le 0) {
        $Jobs = [Environment]::ProcessorCount
    }
    Write-Step "Building (-j $Jobs)"
    cmake --build build -j $Jobs @buildConfigArgs
    if ($LASTEXITCODE -ne 0) { throw 'cmake build failed' }
}

# ---------------------------------------------------------------------------
# 3. Test
# ---------------------------------------------------------------------------
if (-not $SkipBuild -and -not $SkipTests) {
    Write-Step 'Running tests (ctest)'
    Push-Location build
    try {
        ctest --output-on-failure @ctestConfigArgs
        if ($LASTEXITCODE -ne 0) { throw 'tests failed' }
    }
    finally {
        Pop-Location
    }
}

function Find-CppCoderExe {
    # Probe both single-config (build/src/cppcoder) and multi-config
    # (build/src/Debug/cppcoder.exe) layouts rather than trusting
    # $effectiveCompiler, since -SkipBuild may be reusing a build/ made
    # by an earlier, differently-configured run.
    $exeName = if ($OnWindows) { 'cppcoder.exe' } else { 'cppcoder' }
    $candidates = @(
        (Join-Path 'build' (Join-Path 'src' $exeName)),
        (Join-Path 'build' (Join-Path 'src' (Join-Path 'Debug' $exeName))),
        (Join-Path 'build' (Join-Path 'src' (Join-Path 'Release' $exeName)))
    )
    $found = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $found) {
        throw "cppcoder executable not found (looked in: $($candidates -join ', ')) -- " +
              "build first (omit -SkipBuild)."
    }
    return $found
}

# ---------------------------------------------------------------------------
# 4. Optional: research a question
# ---------------------------------------------------------------------------
if ($Question) {
    $exe = Find-CppCoderExe

    if (-not $EventsFile) {
        $EventsFile = Join-Path 'build' 'last_run.jsonl'
    }

    Write-Step "Researching: $Question"
    & $exe --question $Question --codebase $Codebase --model $Model `
        --events-file $EventsFile --log-level $LogLevel
    $exitCode = $LASTEXITCODE

    Write-Step "Events written to $EventsFile (load it in web/index.html to visualize)"
    if ($exitCode -ne 0) {
        Write-Warning "cppcoder exited with code $exitCode (no answer found -- see output above)"
    }
}

# ---------------------------------------------------------------------------
# 5. Optional: open the web UI
# ---------------------------------------------------------------------------
if ($OpenWeb) {
    $webPath = Join-Path $PSScriptRoot 'web/index.html'
    Write-Step "Opening $webPath"
    if ($OnWindows) {
        Start-Process $webPath
    }
    elseif ($OnMacOS) {
        & open $webPath
    }
    else {
        & xdg-open $webPath
    }
}

# ---------------------------------------------------------------------------
# 6. Optional: start the chat server (blocks until Ctrl+C)
# ---------------------------------------------------------------------------
if ($Serve) {
    $exe = Find-CppCoderExe
    $chatUrl = "http://${ServeHost}:${ServePort}/chat.html"

    Write-Step "Starting chat server at http://${ServeHost}:${ServePort}"
    Write-Host "  (model: $Model -- switch models from the dropdown once it's up)"

    # Open the browser shortly after the server has had a moment to bind,
    # then block in the foreground running the server itself.
    $openJob = Start-Job -ScriptBlock {
        param($Url, $OnWin, $OnMac)
        Start-Sleep -Milliseconds 700
        if ($OnWin) { Start-Process $Url }
        elseif ($OnMac) { & open $Url }
        else { & xdg-open $Url }
    } -ArgumentList $chatUrl, $OnWindows, $OnMacOS

    try {
        & $exe --serve --serve-host $ServeHost --serve-port $ServePort --model $Model `
            --log-level $LogLevel
    }
    finally {
        Remove-Job -Job $openJob -Force -ErrorAction SilentlyContinue
    }
}

Write-Step 'Done'
