#!/bin/bash

IKOS=/Users/baesungmin/master/experiment/ikos/install/bin/ikos
BENCHMARK_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "========================================"
echo " IKOS (modified) benchmark analysis"
echo "========================================"
echo ""

total_safe=0
total_warn=0

pick_analyses() {
    local name="$1"
    local num="${name%%_*}"
    if   [[ "$num" -ge 11 && "$num" -le 14 ]]; then echo "dbz"
    elif [[ "$num" -ge 15 && "$num" -le 17 ]]; then echo "boa"
    elif [[ "$num" -ge 18 && "$num" -le 20 ]]; then echo "dbz,boa"
    else echo "nullity"
    fi
}

for src in "$BENCHMARK_DIR"/1[1-9]_*.cpp "$BENCHMARK_DIR"/20_*.cpp; do
    name=$(basename "$src")
    analyses=$(pick_analyses "$name")
    echo "----------------------------------------"
    echo "[$name]  analyses: $analyses"
    echo "----------------------------------------"
    result=$("$IKOS" "$src" --analyses="$analyses" 2>&1)
    safe=$(echo "$result" | grep "Total number of safe checks" | grep -o '[0-9]*' | tail -1)
    warnings=$(echo "$result" | grep "Total number of warnings" | grep -o '[0-9]*' | tail -1)
    safe=${safe:-0}
    warnings=${warnings:-0}
    total_safe=$((total_safe + safe))
    total_warn=$((total_warn + warnings))
    echo "  safe: $safe  |  warnings: $warnings"
    echo "$result" | grep -E ":[0-9]+:[0-9]+: warning:" | sed 's/^/  /'
    echo ""
done

echo "========================================"
echo " Total — safe: $total_safe  |  warnings: $total_warn"
echo "========================================"
