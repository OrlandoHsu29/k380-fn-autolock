param(
    [string]$Version = '1.0.2',
    [string]$Compiler,
    [string]$InnoCompiler
)

$ErrorActionPreference = 'Stop'

$versionSource = Join-Path $PSScriptRoot 'src\app\app_ui.c'
$expectedVersion = '#define APP_VERSION L"v{0}"' -f $Version
if (-not (Select-String -LiteralPath $versionSource -SimpleMatch $expectedVersion -Quiet)) {
    throw "Application version does not match installer version $Version. Update APP_VERSION first."
}

$buildParameters = @{ Release = $true }
if ($Compiler) {
    $buildParameters.Compiler = $Compiler
}
& (Join-Path $PSScriptRoot 'build.ps1') @buildParameters

if (-not $InnoCompiler) {
    $candidates = @(
        'E:\Inno-Setup-7\ISCC.exe',
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 7\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 7\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 7\ISCC.exe')
    )
    $InnoCompiler = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $InnoCompiler -or -not (Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) {
    throw 'Inno Setup compiler ISCC.exe was not found. Install Inno Setup 7 or pass -InnoCompiler.'
}

$script = Join-Path $PSScriptRoot 'installer\k380-fn-auto-lock.iss'
& $InnoCompiler "/DMyAppVersion=$Version" $script
if ($LASTEXITCODE -ne 0) {
    throw 'Installer build failed.'
}

$installer = Join-Path $PSScriptRoot "dist\K380-Fn-Auto-Lock-v$Version-windows-x64-setup.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "Installer output was not found: $installer"
}
Write-Host "Built installer: $installer"
