#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/macos/clap"
DEST="/Library/Audio/Plug-Ins/CLAP"

if [ "${1:-}" = "--user" ]; then
    DEST="$HOME/Library/Audio/Plug-Ins/CLAP"
fi

if [ ! -d "$BUILD_DIR" ]; then
    echo "No macOS build found. Run ./scripts/build-macos.sh first." >&2
    exit 1
fi

SUDO=""
if [ ! -w "$DEST" ] && [ "$DEST" = "/Library/Audio/Plug-Ins/CLAP" ]; then
    SUDO="sudo"
fi

$SUDO mkdir -p "$DEST"
for bundle in "$BUILD_DIR"/*.clap; do
    name="$(basename "$bundle")"
    $SUDO rm -rf "$DEST/$name"
    $SUDO cp -R "$bundle" "$DEST/$name"
    echo "Installed $DEST/$name"
done

echo "Done! Open FL Studio -> Options -> Manage Plugins -> Find installed plugins."
