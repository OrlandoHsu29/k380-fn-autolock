param([string]$Compiler)

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

$source = Join-Path $PSScriptRoot 'main.c'
$resource = Join-Path $PSScriptRoot 'app-icon.rc'
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

function Invoke-Build([string]$name, [string[]]$extraArgs) {
    $destination = Join-Path $output $name
    & $compilerPath -std=c11 -finput-charset=UTF-8 -g -O0 -Wall -Wextra "-I$include" $source $resourceObject @extraArgs -o $destination "-L$library" -lhidapi -luser32 -lbthprops -lshell32 -ladvapi32
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed: $name"
    }
}

Invoke-Build 'setFnKeys.exe' @()
Invoke-Build 'setMediaKeys.exe' @('-DsetMediaKeys')
Invoke-Build 'k380FnAutoLock.exe' @('-DAUTO_WATCH', '-mwindows')
Copy-Item -LiteralPath (Join-Path $library 'hidapi.dll') -Destination (Join-Path $output 'hidapi.dll') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'hidapi\LICENSE-bsd.txt') -Destination (Join-Path $output 'LICENSE-hidapi-bsd.txt') -Force
Write-Host "Built K380 tools for $architecture in $output"
