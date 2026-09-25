param([string]$Compiler, [switch]$Release)

$ErrorActionPreference = 'Stop'

if ($Compiler) {
    if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
        throw "Compiler not found: $Compiler"
    }
    $compilerPath = (Resolve-Path -LiteralPath $Compiler).Path
} else {
    $command = Get-Command gcc -ErrorAction SilentlyContinue
    if ($command) {
        $compilerPath = $command.Source
    } else {
        throw 'MinGW gcc was not found in PATH. Pass -Compiler with the path to gcc.exe.'
    }
}

$target = (& $compilerPath -dumpmachine).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Could not determine the gcc target architecture.'
}

if ($target -match '^(x86_64|amd64)') {
    $architecture = 'x64'
} elseif ($target -match '^(i[3-6]86|x86)') {
    $architecture = 'x86'
} else {
    throw "Unsupported gcc target: $target"
}

$appSourceDirectory = Join-Path $PSScriptRoot 'src\app'
$deviceSourceDirectory = Join-Path $PSScriptRoot 'src\device'
$source = Join-Path $appSourceDirectory 'main.c'
$hidSource = Join-Path $deviceSourceDirectory 'k380_hid.c'
$settingsSource = Join-Path $appSourceDirectory 'app_settings.c'
$stateSource = Join-Path $appSourceDirectory 'app_state.c'
$uiSource = Join-Path $appSourceDirectory 'app_ui.c'
$resource = Join-Path $PSScriptRoot 'resources\app-icon.rc'
$icon = Join-Path $PSScriptRoot 'media\k380-fn-autolock-logo.ico'
$include = Join-Path $PSScriptRoot 'hidapi\include'
$library = Join-Path $PSScriptRoot "hidapi\$architecture"
$output = Join-Path $PSScriptRoot 'build'
$windres = Join-Path (Split-Path -Parent $compilerPath) 'windres.exe'
New-Item -ItemType Directory -Path $output -Force | Out-Null

if (-not (Test-Path -LiteralPath $icon -PathType Leaf)) {
    throw "Application icon not found: $icon"
}
if (-not (Test-Path -LiteralPath $windres -PathType Leaf)) {
    $windresCommand = Get-Command windres -ErrorAction SilentlyContinue
    if ($windresCommand) {
        $windres = $windresCommand.Source
    } else {
        throw 'windres.exe was not found next to gcc.exe or in PATH.'
    }
}

$resourceObject = Join-Path $output 'app-icon.o'
& $windres -i $resource -o $resourceObject -I $PSScriptRoot
if ($LASTEXITCODE -ne 0) {
    throw 'Could not compile the application icon resource.'
}

$optimizationArgs = if ($Release) { @('-Os', '-s') } else { @('-g', '-O0') }

function Invoke-Build([string]$name, [string[]]$sourceFiles, [string[]]$extraArgs) {
    $destination = Join-Path $output $name
    & $compilerPath -std=c11 -finput-charset=UTF-8 @optimizationArgs -Wall -Wextra "-I$include" "-I$appSourceDirectory" "-I$deviceSourceDirectory" @sourceFiles $resourceObject @extraArgs -o $destination "-L$library" -lhidapi -luser32 -lbthprops -lshell32 -ladvapi32
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed: $name"
    }
}

Invoke-Build 'setFnKeys.exe' @($source, $hidSource) @()
Invoke-Build 'setMediaKeys.exe' @($source, $hidSource) @('-DsetMediaKeys')
Invoke-Build 'k380FnAutoLock.exe' @($source, $hidSource, $settingsSource, $stateSource, $uiSource) @('-DAUTO_WATCH', '-mwindows')
Copy-Item -LiteralPath (Join-Path $library 'hidapi.dll') -Destination (Join-Path $output 'hidapi.dll') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'hidapi\LICENSE-bsd.txt') -Destination (Join-Path $output 'LICENSE-hidapi-bsd.txt') -Force
Write-Host "Built K380 tools for $architecture in $output"
