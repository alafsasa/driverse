#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

echo "Building Driverse..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make -j$(nproc)
echo "Build complete: $BUILD_DIR/driverse"
