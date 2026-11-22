#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MPC_ROOT="$(cd "$ROOT/.." && pwd)"

sync_one() {
    local src_dir="$1"
    local dst_slug="$2"
    if [ ! -d "$MPC_ROOT/$src_dir/src" ]; then
        echo "Skipping $src_dir (not found at $MPC_ROOT/$src_dir)"
        return
    fi
    rm -rf "$ROOT/plugins/$dst_slug/src"
    cp -R "$MPC_ROOT/$src_dir/src" "$ROOT/plugins/$dst_slug/src"
    echo "Synced $src_dir -> plugins/$dst_slug"
}

sync_one "Anvil"          "anvil"
sync_one "Cortex Filter"  "cortex_filter"
sync_one "Foundry"        "foundry"
sync_one "Machine-engine" "machine"
sync_one "MoonBass"       "moonbass"
sync_one "Mystik"         "mystik"
sync_one "Necronomicon"   "necronomicon"
sync_one "Nexus"          "nexus"
sync_one "Psychic"        "psychic"
sync_one "Reverie"        "reverie"
sync_one "TB-MPC-3-P-0"   "tb_mpc_3p0"

if [ -f "$MPC_ROOT/Foundry/build/screen-art.h" ]; then
    cp "$MPC_ROOT/Foundry/build/screen-art.h" "$ROOT/plugins/foundry/src/screen-art.h"
fi
if [ -f "$MPC_ROOT/Necronomicon/build/ui/liquid_art.h" ]; then
    cp "$MPC_ROOT/Necronomicon/build/ui/liquid_art.h" "$ROOT/plugins/necronomicon/src/liquid_art.h"
fi
if [ -f "$MPC_ROOT/Reverie/build/voicings.inc" ]; then
    cp "$MPC_ROOT/Reverie/build/voicings.inc" "$ROOT/plugins/reverie/src/voicings.inc"
fi

echo "Sync complete."
