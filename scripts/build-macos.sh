#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/macos"

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"

echo "Ad-hoc signing macOS .clap bundles..."
for bundle in "$BUILD_DIR"/clap/*.clap; do
    codesign --force --deep --sign - "$bundle"
done

echo "Running CLAP host smoke tests..."
"$BUILD_DIR/host_test" "$BUILD_DIR"/clap/*.clap
