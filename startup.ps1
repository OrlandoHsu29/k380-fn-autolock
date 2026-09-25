param([switch]$Remove)

$ErrorActionPreference = 'Stop'
$entryName = 'K380FnAutoLock'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$builtExecutable = Join-Path $PSScriptRoot 'build\k380FnAutoLock.exe'
$builtLibrary = Join-Path $PSScriptRoot 'build\hidapi.dll'
$builtLibraryLicense = Join-Path $PSScriptRoot 'build\LICENSE-hidapi-bsd.txt'
$installDirectory = Join-Path $PSScriptRoot 'installed'
$executable = Join-Path $installDirectory 'k380FnAutoLock.exe'

function Stop-ProjectWatcher {
    Get-Process -Name 'k380FnAutoLock' -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq $executable -or $_.Path -eq $builtExecutable } |
        Stop-Process -PassThru |
        Wait-Process
}

if ($Remove) {
    Remove-ItemProperty -Path $runKey -Name $entryName -ErrorAction SilentlyContinue
    Stop-ProjectWatcher
    Write-Host 'K380 Fn auto lock startup entry removed.'
    return
}

if (-not (Test-Path -LiteralPath $builtExecutable -PathType Leaf)) {
    throw "Build k380FnAutoLock.exe first: $builtExecutable"
}
if (-not (Test-Path -LiteralPath $builtLibrary -PathType Leaf)) {
    throw 'hidapi.dll is missing from build.'
}
if (-not (Test-Path -LiteralPath $builtLibraryLicense -PathType Leaf)) {
    throw 'HIDAPI license is missing from build.'
}

Stop-ProjectWatcher
New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
Copy-Item -LiteralPath $builtExecutable -Destination $executable -Force
Copy-Item -LiteralPath $builtLibrary -Destination (Join-Path $installDirectory 'hidapi.dll') -Force
Copy-Item -LiteralPath $builtLibraryLicense -Destination (Join-Path $installDirectory 'LICENSE-hidapi-bsd.txt') -Force
New-Item -Path $runKey -Force | Out-Null
New-ItemProperty -Path $runKey -Name $entryName -PropertyType String -Value ('"{0}" --background' -f $executable) -Force | Out-Null
$process = Start-Process -FilePath $executable -ArgumentList '--background' -WindowStyle Hidden -PassThru
Start-Sleep -Milliseconds 500
$process.Refresh()
if ($process.HasExited) {
    throw "K380 Fn auto lock exited immediately with code $($process.ExitCode)."
}
Write-Host "K380 Fn auto lock is running (PID $($process.Id)) and will start when Windows starts."
