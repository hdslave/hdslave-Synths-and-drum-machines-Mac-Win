#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/macos/clap"
DEST="$HOME/Library/Audio/Plug-Ins/CLAP"

if [ ! -d "$BUILD_DIR" ]; then
    echo "No macOS build found. Run ./scripts/build-macos.sh first." >&2
    exit 1
fi

mkdir -p "$DEST"
for bundle in "$BUILD_DIR"/*.clap; do
    name="$(basename "$bundle")"
    rm -rf "$DEST/$name"
    cp -R "$bundle" "$DEST/$name"
    echo "Installed $DEST/$name"
done

echo "Done! Open FL Studio -> Options -> Manage Plugins -> Find installed plugins."
