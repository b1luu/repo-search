#!/usr/bin/env bash
# check_format.sh — verify all source files match clang-format style.
# Exits non-zero and lists offending files if any formatting diff exists.
# Used by CI; run locally before committing.
# Usage: ./scripts/check_format.sh
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

FAILED=()
for f in "${FILES[@]}"; do
    # --dry-run --Werror exits non-zero if the file would be changed
    if ! "$CF" --dry-run --Werror --style=file "$f" &>/dev/null; then
        FAILED+=("$f")
    fi
done

if [[ ${#FAILED[@]} -eq 0 ]]; then
    echo "Format check passed (${#FILES[@]} file(s))."
    exit 0
fi

echo "Format check FAILED — ${#FAILED[@]} file(s) need reformatting:"
for f in "${FAILED[@]}"; do
    echo "  $f"
done
echo ""
echo "Run ./scripts/format.sh to fix, then re-stage."
exit 1
