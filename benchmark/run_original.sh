#!/bin/bash

IKOS=/Users/baesungmin/Projects/ikos/install/bin/ikos
BENCHMARK_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS_DIR="${IKOS_BENCHMARK_OUTPUT:-$(mktemp -d "${TMPDIR:-/tmp}/ikos-original-1-50.XXXXXX")}"
mkdir -p "$RESULTS_DIR" || exit 1
printf 'file\tanalyses\tsafe\tunsafe\twarnings\texit_status\n' > "$RESULTS_DIR/summary.tsv"

echo "========================================"
echo " IKOS (original) benchmark analysis"
echo "========================================"
echo " Results: $RESULTS_DIR"
echo ""

total_safe=0
total_unsafe=0
total_warn=0
total_failed=0

pick_analyses() {
    local src="$1"
    local declared
    declared=$(sed -n 's|^// analysis:[[:space:]]*||p' "$src" | head -1 | tr -d '[:space:]')
    if [[ -n "$declared" ]]; then
        echo "$declared"
        return
    fi
    local name="${src##*/}"
    local num=$((10#${name%%_*}))
    if [[ "$num" -eq 6 ]]; then echo "boa"
    elif [[ "$num" -le 10 ]]; then echo "nullity"
    else echo "dbz"
    fi
}

for src in "$BENCHMARK_DIR"/0[1-9]_*.cpp "$BENCHMARK_DIR"/[1-4][0-9]_*.cpp "$BENCHMARK_DIR"/50_*.cpp; do
    [[ -f "$src" ]] || continue
    name=$(basename "$src")
    analyses=$(pick_analyses "$src")
    echo "----------------------------------------"
    echo "[$name]  analyses: $analyses"
    echo "----------------------------------------"
    "$IKOS" "$src" --analyses="$analyses" \
        -o "$RESULTS_DIR/${name%.cpp}.db" > "$RESULTS_DIR/${name%.cpp}.log" 2>&1
    analysis_status=$?
    result=$(cat "$RESULTS_DIR/${name%.cpp}.log")
    safe=$(echo "$result" | grep "Total number of safe checks" | grep -o '[0-9]*' | tail -1)
    unsafe=$(echo "$result" | grep "Total number of definite unsafe checks" | grep -o '[0-9]*' | tail -1)
    warnings=$(echo "$result" | grep "Total number of warnings" | grep -o '[0-9]*' | tail -1)
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$analyses" "$safe" "$unsafe" "$warnings" "$analysis_status" >> "$RESULTS_DIR/summary.tsv"
    if [[ "$analysis_status" -ne 0 || -z "$safe" || -z "$unsafe" || -z "$warnings" ]]; then
        total_failed=$((total_failed + 1))
        echo "  FAILED: analysis exit code $analysis_status (no valid check summary)"
        printf '%s\n' "$result"
        echo ""
        continue
    fi
    total_safe=$((total_safe + safe))
    total_unsafe=$((total_unsafe + unsafe))
    total_warn=$((total_warn + warnings))
    echo "  safe: $safe  |  definite unsafe: $unsafe  |  warnings: $warnings"
    echo "$result" | grep -E ":[0-9]+:[0-9]+: (warning|error):" | sed 's/^/  /'
    echo "  [Symbolic Summary] (none — original IKOS)"
    echo ""
done

echo "========================================"
echo " Total — safe: $total_safe  |  definite unsafe: $total_unsafe  |  warnings: $total_warn  |  failed: $total_failed"
echo "========================================"

# A failed analysis must not be reported as a successful zero-warning run.
[[ "$total_failed" -eq 0 ]]
