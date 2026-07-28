# CppCoder / Ollama connectivity diagnostic
# Run from repo root: ~/local/repos/CppCoder

$out = "diag_output.txt"
"" | Out-File $out

function Section($title) {
    "`n===== $title =====" | Out-File $out -Append
}

Section "Ollama version"
ollama --version 2>&1 | Out-File $out -Append

Section "Ollama /api/tags"
try {
    $r = Invoke-WebRequest -Uri "http://127.0.0.1:11434/api/tags" -UseBasicParsing
    "Status: $($r.StatusCode)" | Out-File $out -Append
    $r.Content | Out-File $out -Append
} catch {
    "FAILED: $($_.Exception.Message)" | Out-File $out -Append
}

Section "Ollama /api/version"
try {
    $r = Invoke-WebRequest -Uri "http://127.0.0.1:11434/api/version" -UseBasicParsing
    "Status: $($r.StatusCode)" | Out-File $out -Append
    $r.Content | Out-File $out -Append
} catch {
    "FAILED: $($_.Exception.Message)" | Out-File $out -Append
}

Section "CppCoder server root"
try {
    $r = Invoke-WebRequest -Uri "http://127.0.0.1:8765/" -UseBasicParsing
    "Status: $($r.StatusCode)" | Out-File $out -Append
} catch {
    "FAILED: $($_.Exception.Message)" | Out-File $out -Append
}

Section "CppCoder source: Ollama URL/path references"
Get-ChildItem -Path .\src, .\include -Recurse -Include *.cpp,*.h,*.hpp -ErrorAction SilentlyContinue |
    Select-String -Pattern "11434|/api/|ollama" -CaseSensitive:$false |
    ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" } |
    Out-File $out -Append

Section "CppCoder web/chat.html: fetch/API references"
Get-ChildItem -Path .\web -Recurse -Include *.html,*.js -ErrorAction SilentlyContinue |
    Select-String -Pattern "fetch\(|/api/|11434|8765" |
    ForEach-Object { "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" } |
    Out-File $out -Append

Section "Listening ports (11434 / 8765)"
Get-NetTCPConnection -LocalPort 11434,8765 -ErrorAction SilentlyContinue |
    Select-Object LocalAddress,LocalPort,State,OwningProcess |
    Format-Table | Out-File $out -Append

Section "Running ollama/cppcoder processes"
Get-Process | Where-Object { $_.ProcessName -match "ollama|cppcoder" } |
    Select-Object Id,ProcessName,StartTime |
    Format-Table | Out-File $out -Append

Section "cppcoder.exe version/help"
try {
    & .\build\src\cppcoder.exe --help 2>&1 | Out-File $out -Append
} catch {
    "FAILED to run --help: $($_.Exception.Message)" | Out-File $out -Append
}

Write-Host "Done. Results written to $out"
Write-Host "Paste the contents of $out back into chat."
