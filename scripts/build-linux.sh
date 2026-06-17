#!/usr/bin/env bash
# Build CristiVerse on Linux/macOS.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR="${1:-build}"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j "$(nproc 2>/dev/null || echo 4)"

echo
echo "Built: $BUILD_DIR/bin/CristiVerse"
echo "Run it with: ./$BUILD_DIR/bin/CristiVerse"
