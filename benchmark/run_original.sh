#!/bin/bash

IKOS=/Users/baesungmin/Projects/ikos/install/bin/ikos
BENCHMARK_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "========================================"
echo " IKOS (original) benchmark analysis"
echo "========================================"
echo ""

total_safe=0
total_warn=0
total_failed=0

pick_analyses() {
    local name="$1"
    local num="${name%%_*}"
    if   [[ "$num" -eq 26 || "$num" -eq 29 || "$num" -eq 30 ]]; then echo "boa"
    else echo "dbz"
    fi
}

for src in "$BENCHMARK_DIR"/2[1-9]_*.cpp "$BENCHMARK_DIR"/30_*.cpp; do
    [[ -f "$src" ]] || continue
    name=$(basename "$src")
    analyses=$(pick_analyses "$name")
    echo "----------------------------------------"
    echo "[$name]  analyses: $analyses"
    echo "----------------------------------------"
    result=$("$IKOS" "$src" --analyses="$analyses" 2>&1)
    analysis_status=$?
    safe=$(echo "$result" | grep "Total number of safe checks" | grep -o '[0-9]*' | tail -1)
    warnings=$(echo "$result" | grep "Total number of warnings" | grep -o '[0-9]*' | tail -1)
    if [[ "$analysis_status" -ne 0 || -z "$safe" || -z "$warnings" ]]; then
        total_failed=$((total_failed + 1))
        echo "  FAILED: analysis exit code $analysis_status (no valid check summary)"
        printf '%s\n' "$result"
        echo ""
        continue
    fi
    total_safe=$((total_safe + safe))
    total_warn=$((total_warn + warnings))
    echo "  safe: $safe  |  warnings: $warnings"
    echo "$result" | grep -E ":[0-9]+:[0-9]+: warning:" | sed 's/^/  /'
    echo "  [Symbolic Summary] (none — original IKOS)"
    echo ""
done

echo "========================================"
echo " Total — safe: $total_safe  |  warnings: $total_warn  |  failed: $total_failed"
echo "========================================"

# A failed analysis must not be reported as a successful zero-warning run.
[[ "$total_failed" -eq 0 ]]
