#!/usr/bin/env bash
# lint.sh — run clang-tidy over src/ using the debug build's compile_commands.json.
# Requires: compile_commands.json at the project root (run build_debug.sh first).
# Usage: ./scripts/lint.sh [extra clang-tidy flags]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILE_DB="$REPO_ROOT/compile_commands.json"

find_clang_tidy() {
    if [[ -n "${CLANG_TIDY:-}" ]]; then
        echo "$CLANG_TIDY"
        return
    fi
    for candidate in clang-tidy clang-tidy-18 clang-tidy-17 \
                      clang-tidy-16 clang-tidy-15 clang-tidy-14; do
        if command -v "$candidate" &>/dev/null; then
            echo "$candidate"
            return
        fi
    done
    echo ""
}

CT="$(find_clang_tidy)"
if [[ -z "$CT" ]]; then
    echo "error: clang-tidy not found. Install it or set CLANG_TIDY=/path/to/clang-tidy." >&2
    exit 1
fi

if [[ ! -f "$COMPILE_DB" ]]; then
    echo "error: compile_commands.json not found at project root." >&2
    echo "Run ./scripts/build_debug.sh first — it generates and symlinks the database." >&2
    exit 1
fi

echo "Using: $(command -v "$CT") ($("$CT" --version | head -1))"

# Collect source files (bash 3.x compatible — avoids mapfile)
FILES=()
while IFS= read -r f; do
    FILES+=("$f")
done < <(find "$REPO_ROOT/src" -name '*.cpp' | sort)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "No source files found."
    exit 0
fi

echo "Linting ${#FILES[@]} file(s)..."
"$CT" -p "$REPO_ROOT" "$@" "${FILES[@]}"
