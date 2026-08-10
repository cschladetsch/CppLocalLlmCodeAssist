<#
.SYNOPSIS
    Builds (unless -SkipBuild) and installs the cppcoder binary, renamed
    to 'sarah', into a user-local bin directory -- added to your PATH if
    it isn't there already. No admin/sudo rights needed.

.PARAMETER SkipBuild
    Skip the build step and install whatever's already in build/.

.PARAMETER Clean
    Full rebuild first (forwarded to t.ps1 -Clean).

.PARAMETER Destination
    Directory to install the renamed binary into. Defaults to
    %LOCALAPPDATA%\Programs\sarah on Windows, or ~/.local/bin elsewhere.

.EXAMPLE
    ./install.ps1
    Build (if needed) and install as 'sarah' into the default user-local
    bin directory, adding that directory to PATH.

.EXAMPLE
    ./install.ps1 -SkipBuild
    Reuse the existing build/ and just (re)install.

.EXAMPLE
    ./install.ps1 -Clean -Destination ~/bin
    Full rebuild, then install into a custom directory.
#>

[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$Clean,
    [string]$Destination
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# See t.ps1 for why this guards against Windows PowerShell 5.1 (Desktop
# edition), where $IsWindows doesn't exist.
if ($PSVersionTable.PSEdition -eq 'Core') {
    $OnWindows = $IsWindows
}
else {
    $OnWindows = $true
}

function Write-Step {
    param([string]$Message)
    Write-Host "==> $Message" -ForegroundColor Cyan
}

if (-not $SkipBuild) {
    Write-Step 'Building cppcoder (delegating to t.ps1)'
    # Hashtable splat, not array splat: array splatting binds positionally
    # (switches are excluded from position numbering, so a bare
    # '-SkipTests' string would land on t.ps1's first non-switch
    # parameter, $Jobs, and fail its int conversion). Only hashtable
    # splatting binds by parameter name.
    $buildArgs = @{ SkipTests = $true }
    if ($Clean) { $buildArgs['Clean'] = $true }
    & (Join-Path $PSScriptRoot 't.ps1') @buildArgs
    if ($LASTEXITCODE -ne 0) { throw 't.ps1 build failed' }
}

# Probe both single-config (build/src/cppcoder) and multi-config
# (build/src/Debug/cppcoder.exe) layouts -- same candidates t.ps1's
# Find-CppCoderExe checks, since -SkipBuild may be reusing a build/ made
# by an earlier, differently-configured run.
$exeName = if ($OnWindows) { 'cppcoder.exe' } else { 'cppcoder' }
$candidates = @(
    (Join-Path 'build' (Join-Path 'src' $exeName)),
    (Join-Path 'build' (Join-Path 'src' (Join-Path 'Debug' $exeName))),
    (Join-Path 'build' (Join-Path 'src' (Join-Path 'Release' $exeName)))
)
$sourceExe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $sourceExe) {
    throw ("cppcoder executable not found (looked in: $($candidates -join ', ')) -- " +
           'build first (omit -SkipBuild).')
}

if (-not $Destination) {
    $Destination = if ($OnWindows) {
        Join-Path $env:LOCALAPPDATA 'Programs\sarah'
    }
    else {
        Join-Path $HOME '.local/bin'
    }
}
$Destination = [System.IO.Path]::GetFullPath($Destination)

if (-not (Test-Path $Destination)) {
    Write-Step "Creating $Destination"
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
}

$targetName = if ($OnWindows) { 'sarah.exe' } else { 'sarah' }
$targetPath = Join-Path $Destination $targetName

Write-Step "Installing $sourceExe -> $targetPath"
Copy-Item -Path $sourceExe -Destination $targetPath -Force
if (-not $OnWindows) {
    chmod +x $targetPath
}

# ---------------------------------------------------------------------------
# Add $Destination to the user's PATH if it isn't already there.
# ---------------------------------------------------------------------------
function Add-DestinationToPath {
    param([string]$Dir)

    if ($OnWindows) {
        # The registry-backed 'User' scope persists across terminals and
        # reboots but isn't broadcast to already-running processes, so
        # also patch this session's $env:PATH below for immediate use.
        $currentPath = [Environment]::GetEnvironmentVariable('PATH', 'User')
        $entries = @(($currentPath -split ';') | Where-Object { $_ })
        if ($entries -contains $Dir) {
            return $false
        }
        $newPath = if ($currentPath) { "$currentPath;$Dir" } else { $Dir }
        [Environment]::SetEnvironmentVariable('PATH', $newPath, 'User')
        $env:PATH = "$env:PATH;$Dir"
        return $true
    }

    $entries = @(($env:PATH -split ':') | Where-Object { $_ })
    if ($entries -contains $Dir) {
        return $false
    }
    # ~/.profile is sourced by bash and zsh login shells alike, unlike
    # shell-specific rc files, so it's the one place that reliably picks
    # this up regardless of which shell the user runs `sarah` from next.
    $profileFile = Join-Path $HOME '.profile'
    $alreadyThere = (Test-Path $profileFile) -and
                    (Select-String -Path $profileFile -SimpleMatch $Dir -Quiet)
    if (-not $alreadyThere) {
        Add-Content -Path $profileFile -Value "`n# Added by CppCoder's install.ps1`nexport PATH=`"`$PATH:$Dir`""
    }
    $env:PATH = "$env:PATH:$Dir"
    return $true
}

if (Add-DestinationToPath -Dir $Destination) {
    $restartNote = if ($OnWindows) { 'open a new terminal' } else { 'start a new shell, or run: source ~/.profile' }
    Write-Step "Added $Destination to your PATH ($restartNote to pick it up elsewhere)"
}
else {
    Write-Step "$Destination is already on PATH"
}

Write-Step "Installed '$targetName'. This session can already run it -- try: sarah --help"
