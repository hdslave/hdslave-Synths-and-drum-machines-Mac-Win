$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Plugins = @("Anvil", "Cortex-Filter", "Foundry", "Machine", "MoonBass", "Mystik", "Necronomicon", "Nexus", "Psychic", "Reverie", "Slink")

foreach ($p in $Plugins) {
    $Script = Join-Path $Root "$p\mac-clap\build-windows.ps1"
    if (-not (Test-Path $Script)) {
        Write-Error "Missing $Script — run 'git submodule update --init' first."
        exit 1
    }
    Write-Host "=== Building $p (Windows x64 CLAP) ==="
    & $Script
}

Write-Host "All 11 Windows x64 CLAP plug-ins built and tested!"
