#!/usr/bin/env bash
# format.sh — apply clang-format to all project source files in-place.
# Usage: ./scripts/format.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

find_clang_format() {
    if [[ -n "${CLANG_FORMAT:-}" ]]; then
        echo "$CLANG_FORMAT"
        return
    fi
    for candidate in clang-format clang-format-18 clang-format-17 \
                      clang-format-16 clang-format-15 clang-format-14; do
        if command -v "$candidate" &>/dev/null; then
            echo "$candidate"
            return
        fi
    done
    echo ""
}

CF="$(find_clang_format)"
if [[ -z "$CF" ]]; then
    echo "error: clang-format not found. Install it or set CLANG_FORMAT=/path/to/clang-format." >&2
    exit 1
fi

echo "Using: $(command -v "$CF") ($("$CF" --version | head -1))"

# Collect source files (bash 3.x compatible — avoids mapfile)
FILES=()
while IFS= read -r f; do
    FILES+=("$f")
done < <(find "$REPO_ROOT/src" "$REPO_ROOT/tests" \
             \( -name '*.cpp' -o -name '*.h' \) | sort)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "No source files found."
    exit 0
fi

echo "Formatting ${#FILES[@]} file(s)..."
"$CF" -i --style=file "${FILES[@]}"
echo "Done."
