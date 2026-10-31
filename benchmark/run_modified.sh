#!/bin/bash

IKOS=/Users/baesungmin/master/experiment/ikos/install/bin/ikos
BENCHMARK_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "========================================"
echo " IKOS (modified) benchmark analysis"
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
    result=$("$IKOS" "$src" --analyses="$analyses" --trace-ar-stmts 2>&1)
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
    echo ""
    echo "  [Symbolic Summary]"
    echo "$result" | awk '
      function emit_condition(component, level, constraint, cmd, demangled) {
        cmd = "/opt/homebrew/opt/llvm/bin/llvm-cxxfilt " fn
        cmd | getline demangled
        close(cmd)
        if (component == "requires")
          print "    Requires[" demangled "]: " constraint
        else if (component == "program")
          print "    program_exit[" demangled "]: " constraint
        else
          print "    C_" component "[" demangled "] [" level "]: " constraint
      }
      /^Function: /{
        fn = substr($0, length("Function: ") + 1)
        coverage = "Unknown"
      }
      /^[[:space:]]+coverage: /{
        coverage = $NF
      }
      /^    Writable\(/{
        requirement = $0
        sub(/^[[:space:]]+/, "", requirement)
        emit_condition("requires", "Entry", requirement)
      }
      /^\[program_exit\] /{
        description = substr($0, length("[program_exit] ") + 1)
        sub(/[[:space:]]*\(no summary -- process terminates\)$/, "", description)
        if (description == "none")
          description = "none (local CFG)"
        else if (description == "")
          description = "present (unnamed CFG block; no condition summary)"
        else
          description = "present (CFG block " description "; no condition summary)"
        emit_condition("program", "", description)
      }
      /^\[(normal|exception)_exit\] /{
        component = ($0 ~ /^\[normal_exit\]/) ? "normal" : "exception"
        if ($0 ~ /none \(Complete\)/)
          emit_condition(component, "Complete", "false")
        else if ($0 ~ /no certified region \(Partial\)/)
          emit_condition(component, "Partial", "false")
        else if ($0 ~ /unavailable/)
          emit_condition(component, "Unavailable", "unknown")
      }
      /C_(normal|exception) \(parameter/{
        component = ($0 ~ /C_normal/) ? "normal" : "exception"
        constraint = ""
        while ((getline line) > 0) {
          if (line ~ /^\[/ || line == "") break
          gsub(/^[[:space:]]+/, "", line)
          if (constraint == "") constraint = line
          else constraint = constraint " " line
        }
        emit_condition(component, coverage, constraint)
      }
    '
    echo ""
done

echo "========================================"
echo " Total — safe: $total_safe  |  definite unsafe: $total_unsafe  |  warnings: $total_warn  |  failed: $total_failed"
echo "========================================"

# A failed analysis must not be reported as a successful zero-warning run.
[[ "$total_failed" -eq 0 ]]
