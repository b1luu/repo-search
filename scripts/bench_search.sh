#!/usr/bin/env bash
# Benchmark harness for repo_search.
#
# Usage:
#   ./scripts/bench_search.sh <corpus_dir> <query>
#
# Env vars:
#   RUNS  number of iterations       (default 7)
#   BIN   path to repo_search binary (default ./build/debug/repo_search)
#
# Output:
#   Per-run: Parse ms, Index ms, Graph ms, Search µs
#   Summary: min / median / p95 search µs, median for parse/index/graph ms
#
# Portability: targets bash 3 — no mapfile, no associative arrays.

set -u

PROG=$(basename "$0")

die() {
    printf 'bench_search: %s\n' "$*" >&2
    exit 1
}

if [ $# -ne 2 ]; then
    printf 'Usage: %s <corpus_dir> <query>\n' "$PROG" >&2
    printf '  env: RUNS (default 7)  BIN (default ./build/debug/repo_search)\n' >&2
    exit 2
fi

CORPUS=$1
QUERY=$2
RUNS=${RUNS:-7}
BIN=${BIN:-./build/debug/repo_search}

case "$RUNS" in
    ''|*[!0-9]*) die "RUNS must be a positive integer (got '$RUNS')" ;;
esac
[ "$RUNS" -ge 1 ] || die "RUNS must be >= 1"

[ -x "$BIN" ] || die "binary not found or not executable: $BIN"
[ -d "$CORPUS" ] || die "corpus directory not found: $CORPUS"

printf 'bench: bin=%s  corpus=%s  query=%q  runs=%d\n\n' \
    "$BIN" "$CORPUS" "$QUERY" "$RUNS"

printf '  %-4s  %10s  %10s  %10s  %12s\n' \
    'run' 'parse(ms)' 'index(ms)' 'graph(ms)' 'search(us)'
printf '  %-4s  %10s  %10s  %10s  %12s\n' \
    '---' '---------' '---------' '---------' '----------'

# bash 3: plain indexed arrays only.
parse_vals=()
index_vals=()
graph_vals=()
search_vals=()

i=1
while [ "$i" -le "$RUNS" ]; do
    out=$("$BIN" "$CORPUS" "$QUERY" 2>&1) || die "run $i failed:
$out"

    # Extract the numeric value inside "(… ms)" or "(… µs)" on each line.
    # awk handles the multi-byte µ transparently since we match on ASCII tokens.
    parse_ms=$(printf '%s\n' "$out" \
        | awk '/^Parse:/  { for (j=1;j<=NF;j++) if ($j ~ /^\(/) { gsub(/[()]/,"",$j); print $j; exit } }')
    index_ms=$(printf '%s\n' "$out" \
        | awk '/^Index:/  { for (j=1;j<=NF;j++) if ($j ~ /^\(/) { gsub(/[()]/,"",$j); print $j; exit } }')
    graph_ms=$(printf '%s\n' "$out" \
        | awk '/^Graph:/  { for (j=1;j<=NF;j++) if ($j ~ /^\(/) { gsub(/[()]/,"",$j); print $j; exit } }')
    search_us=$(printf '%s\n' "$out" \
        | awk '/^Search:/ { for (j=1;j<=NF;j++) if ($j ~ /^\(/) { gsub(/[()]/,"",$j); print $j; exit } }')

    if [ -z "$parse_ms" ] || [ -z "$index_ms" ] || [ -z "$graph_ms" ] || [ -z "$search_us" ]; then
        die "run $i: failed to parse timing output:
$out"
    fi

    parse_vals[${#parse_vals[@]}]=$parse_ms
    index_vals[${#index_vals[@]}]=$index_ms
    graph_vals[${#graph_vals[@]}]=$graph_ms
    search_vals[${#search_vals[@]}]=$search_us

    printf '  %-4d  %10s  %10s  %10s  %12s\n' \
        "$i" "$parse_ms" "$index_ms" "$graph_ms" "$search_us"

    i=$((i + 1))
done

# Compute min / median / p95 via awk. Args: <label> <unit> <values...>
stats() {
    label=$1; unit=$2; shift 2
    printf '%s\n' "$@" | awk -v label="$label" -v unit="$unit" '
        { v[NR]=$1 }
        END {
            n=NR
            # insertion sort (n is tiny)
            for (i=2;i<=n;i++) { x=v[i]; j=i-1;
                while (j>=1 && v[j]+0 > x+0) { v[j+1]=v[j]; j-- }
                v[j+1]=x
            }
            min=v[1]; max=v[n]
            if (n%2) median=v[(n+1)/2]
            else     median=(v[n/2]+v[n/2+1])/2
            # nearest-rank p95
            p95_idx=int((95/100)*n + 0.9999); if (p95_idx<1) p95_idx=1; if (p95_idx>n) p95_idx=n
            p95=v[p95_idx]
            printf "  %-14s  min=%s %s  median=%s %s  p95=%s %s  max=%s %s\n",
                   label, min, unit, median, unit, p95, unit, max, unit
        }'
}

# Median-only helper.
median() {
    label=$1; unit=$2; shift 2
    printf '%s\n' "$@" | awk -v label="$label" -v unit="$unit" '
        { v[NR]=$1 }
        END {
            n=NR
            for (i=2;i<=n;i++) { x=v[i]; j=i-1;
                while (j>=1 && v[j]+0 > x+0) { v[j+1]=v[j]; j-- }
                v[j+1]=x
            }
            if (n%2) m=v[(n+1)/2]
            else     m=(v[n/2]+v[n/2+1])/2
            printf "  %-14s  median=%s %s\n", label, m, unit
        }'
}

printf '\nsummary (%d runs):\n' "$RUNS"
stats   'search'  'us' "${search_vals[@]}"
median  'parse'   'ms' "${parse_vals[@]}"
median  'index'   'ms' "${index_vals[@]}"
median  'graph'   'ms' "${graph_vals[@]}"
