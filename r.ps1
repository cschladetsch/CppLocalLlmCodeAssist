<#
.SYNOPSIS
    Thin launcher for the interactive chat web UI: builds (unless
    -SkipBuild), then runs `cppcoder --serve` and opens web/chat.html in
    your browser. Delegates all build/run logic to t.ps1 -Serve so there's
    one source of truth -- this is just the friendly one-liner for the
    "start the app" case.
.PARAMETER SkipBuild
    Skip configure/build and reuse whatever's already in build/.
.PARAMETER Clean
    Remove build/ first and do a full rebuild.
.PARAMETER Model
    Default Ollama model tag selected in the chat UI. Defaults to
    qwen2.5-coder:7b. You can still switch models from the dropdown once
    the page is open.
.PARAMETER ServeHost
    Address to bind the chat server to. Defaults to 127.0.0.1.
.PARAMETER ServePort
    Port to bind the chat server to. Defaults to 8765.
.PARAMETER SkipModelCheck
    Skip the Ollama model-availability check before serving.
.EXAMPLE
    ./r.ps1
    Build (if needed) and open the chat UI at http://127.0.0.1:8765/chat.html.
.EXAMPLE
    ./r.ps1 -SkipBuild
    Skip the build, just start serving with whatever's already built.
.EXAMPLE
    ./r.ps1 -Clean -ServePort 9000
    Full rebuild, then serve on port 9000.
#>
[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$Clean,
    [string]$Model = 'qwen2.5-coder:7b',
    [string]$ServeHost = '127.0.0.1',
    [int]$ServePort = 8765,
    [switch]$SkipModelCheck
)
$ErrorActionPreference = 'Stop'

if (-not $SkipModelCheck) {
    Write-Host "Checking Ollama has '$Model' pulled..." -ForegroundColor Cyan
    try {
        $pulled = & ollama list 2>$null
    } catch {
        Write-Host "Could not reach Ollama. Is 'ollama serve' running?" -ForegroundColor Yellow
        Write-Host "Continuing anyway -- the chat UI will show its own connection error if it's still down." -ForegroundColor Yellow
        $pulled = $null
    }

    if ($pulled -and -not ($pulled -match [regex]::Escape($Model))) {
        Write-Host "Model '$Model' is not pulled yet." -ForegroundColor Yellow
        $answer = Read-Host "Pull it now? This may take a few minutes [Y/n]"
        if ($answer -notmatch '^[Nn]') {
            & ollama pull $Model
        } else {
            Write-Host "Skipping pull -- the model dropdown will show 'none pulled' until you run 'ollama pull $Model'." -ForegroundColor Yellow
        }
    }
}

& (Join-Path $PSScriptRoot 't.ps1') `
    -Serve `
    -SkipBuild:$SkipBuild `
    -Clean:$Clean `
    -Model $Model `
    -ServeHost $ServeHost `
    -ServePort $ServePort
