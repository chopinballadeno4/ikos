#!/bin/bash

IKOS=/Users/baesungmin/Projects/ikos/install/bin/ikos
BENCHMARK_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "========================================"
echo " IKOS (original) benchmark analysis"
echo "========================================"
echo ""

total_safe=0
total_unsafe=0
total_warn=0
total_failed=0

pick_analyses() {
    local name="$1"
    local num="${name%%_*}"
    if   [[ "$num" -eq 42 || "$num" -eq 44 || "$num" -eq 46 || "$num" -eq 48 || "$num" -eq 50 ]]; then echo "boa"
    elif [[ "$num" -eq 49 ]]; then echo "nullity"
    else echo "dbz"
    fi
}

for src in "$BENCHMARK_DIR"/4[1-9]_*.cpp "$BENCHMARK_DIR"/50_*.cpp; do
    [[ -f "$src" ]] || continue
    name=$(basename "$src")
    analyses=$(pick_analyses "$name")
    echo "----------------------------------------"
    echo "[$name]  analyses: $analyses"
    echo "----------------------------------------"
    result=$("$IKOS" "$src" --analyses="$analyses" 2>&1)
    analysis_status=$?
    safe=$(echo "$result" | grep "Total number of safe checks" | grep -o '[0-9]*' | tail -1)
    unsafe=$(echo "$result" | grep "Total number of definite unsafe checks" | grep -o '[0-9]*' | tail -1)
    warnings=$(echo "$result" | grep "Total number of warnings" | grep -o '[0-9]*' | tail -1)
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
