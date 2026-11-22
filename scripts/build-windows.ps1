$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = Join-Path $Root "build\windows"

cmake -S $Root -B $BuildDir -A x64
cmake --build $BuildDir --config Release --parallel

$TestExe = Join-Path $BuildDir "Release\host_test.exe"
$Claps = Get-ChildItem (Join-Path $BuildDir "clap\Release\*.clap") | ForEach-Object { $_.FullName }
if (-not $Claps) {
    $Claps = Get-ChildItem (Join-Path $BuildDir "clap\*.clap") | ForEach-Object { $_.FullName }
}

& $TestExe @Claps
Write-Host "Built Windows .clap plugins! Copy them to: C:\Program Files\Common Files\CLAP\"
