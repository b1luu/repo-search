#!/usr/bin/env bash
# build_debug.sh — configure and build a Debug build with ASan/UBSan enabled.
# Also symlinks compile_commands.json to the project root for clang-tidy.
# Usage: ./scripts/build_debug.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build/debug"

# Prefer Ninja if available; fall back to the default (Unix Makefiles).
if command -v ninja &>/dev/null; then
    GENERATOR="-G Ninja"
    JOBS=""
else
    GENERATOR=""
    JOBS="-j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
fi

echo "==> Configuring Debug build in $BUILD_DIR"
cmake -B "$BUILD_DIR" $GENERATOR \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    "$REPO_ROOT"

echo ""
echo "==> Building"
cmake --build "$BUILD_DIR" $JOBS

# Symlink compile_commands.json to the project root so clang-tidy, editors,
# and language servers find it without extra configuration.
ln -sf "$BUILD_DIR/compile_commands.json" "$REPO_ROOT/compile_commands.json"
echo ""
echo "==> compile_commands.json → $BUILD_DIR/compile_commands.json"

echo ""
echo "==> Running tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure
