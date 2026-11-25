#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLUGINS=(Anvil Cortex-Filter Foundry Machine MoonBass Mystik Necronomicon Nexus Psychic Reverie Slink)

for p in "${PLUGINS[@]}"; do
    script="$ROOT/$p/mac-clap/build-macos.sh"
    if [ ! -f "$script" ]; then
        echo "Missing $script — run 'git submodule update --init' first." >&2
        exit 1
    fi
    echo "=== Building $p (macOS CLAP) ==="
    "$script"
done

echo "All 11 macOS CLAP plug-ins built, signed, and passed host_test!"
