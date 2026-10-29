#!/bin/bash

IKOS=/Users/baesungmin/master/experiment/ikos/install/bin/ikos
BENCHMARK_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "========================================"
echo " IKOS (modified) benchmark analysis"
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
    result=$("$IKOS" "$src" --analyses="$analyses" --trace-ar-stmts 2>&1)
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
    echo ""
    echo "  [Symbolic Summary]"
    echo "$result" | awk '
      /^Function: /{
        fn = substr($0, length("Function: ") + 1)
        coverage = "Unknown"
      }
      /^[[:space:]]+coverage: /{
        coverage = $NF
      }
      /C_normal \(parameter/{
        constraint = ""
        while ((getline line) > 0) {
          if (line ~ /^\[/ || line == "") break
          gsub(/^[[:space:]]+/, "", line)
          if (constraint == "") constraint = line
          else constraint = constraint " " line
        }
        if (constraint != "true") {
          cmd = "/opt/homebrew/opt/llvm/bin/llvm-cxxfilt " fn
          cmd | getline demangled
          close(cmd)
          print "    C_normal[" demangled "] [" coverage "]: " constraint
        }
      }
    '
    echo ""
done

echo "========================================"
echo " Total — safe: $total_safe  |  warnings: $total_warn  |  failed: $total_failed"
echo "========================================"

# A failed analysis must not be reported as a successful zero-warning run.
[[ "$total_failed" -eq 0 ]]
