#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLUGINS=(Anvil Cortex-Filter Foundry Machine MoonBass Mystik Necronomicon Nexus Psychic Reverie TB-MPC-3-P-0)

for p in "${PLUGINS[@]}"; do
    script="$ROOT/$p/mac-clap/install-macos.sh"
    if [ ! -f "$script" ]; then
        echo "Missing $script — run 'git submodule update --init' first." >&2
        exit 1
    fi
    echo "=== Building & Installing $p (macOS CLAP) ==="
    "$script" "$@"
done

echo "All 11 macOS CLAP plug-ins installed! Quit (Cmd+Q) and reopen FL Studio to load the updated bundles."
