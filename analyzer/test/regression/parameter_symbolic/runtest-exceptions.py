#!/usr/bin/env python3
"""Check certified exit conditions separately from native checker results."""

import argparse
import pathlib
import runpy
import sqlite3
import tempfile

helpers = runpy.run_path(str(pathlib.Path(__file__).with_name("runtest-expressions.py")))
command, holds = helpers["command"], helpers["holds"]


def component(trace, function, flow):
    marker = "Function: " + function + "\n"
    assert marker in trace, function
    block = trace.split(marker, 1)[1].split("Function: ", 1)[0]
    section = block.split("[" + flow + "_exit]", 1)[1]
    section = section.split("[exception_exit]", 1)[0] if flow == "normal" else section.split("[program_exit]", 1)[0]
    if "none (Complete)" in section:
        return "false", "Complete"
    if "no certified region (Partial)" in section:
        return "false", "Partial"
    level = section.split("coverage: ", 1)[1].splitlines()[0].strip()
    formula = section.split("C_" + flow + " (parameter constraints):\n", 1)[1]
    lines = []
    for line in formula.splitlines():
        if not line.startswith("    "): break
        lines.append(line.strip())
    return " ".join(lines), level


def evaluate(formula, n):
    if formula in ("true", "false"):
        return formula == "true"
    return holds(formula, n)


def main():
    parser = argparse.ArgumentParser()
    for tool in ("clang", "ikos-pp", "ikos-analyzer"):
        parser.add_argument("--" + tool, required=True)
    opts = parser.parse_args()
    source = pathlib.Path(__file__).with_name("exceptions.cpp")
    guards = ["positive31", "normal32", "exception33", "exception34", "swallowed35",
              "pointer36", "rethrow36", "memory37", "mixed39", "split40",
              "ordinary_wrapper31", "catch_and_rethrow", "typed_catch_fallback",
              "memory_wrapper37", "local_memory37", "mismatched_memory37",
              "volatile_memory37", "external_load37", "class41", "user_class41",
              "constructor_throws41", "constructor_aborts41", "unknown_constructor41",
              "unknown_cleanup41", "catch_unknown_cleanup41", "catch_aborting_cleanup41",
              "catch_std_class41", "rethrow_std_class41", "nondeterministic_constructor41",
              "unknown_message41", "overlapping_class41", "affine_leaf43",
              "affine_mid43", "affine_outer43", "affine_cast43",
              "affine_large_coefficient43", "affine_reordered43",
              "affine_negative_scale43", "affine_wrapped_input43",
              "affine_truncated_input43", "affine_equivalent_bounds43",
              "affine_explicit_limit43", "modular_row44", "modular_col44",
              "modular_shift_guard44", "modular_composed44",
              "modular_wrapped_product44", "modular_different_operands44",
              "modular_opaque44", "modular_coefficient44", "scalar_strict45",
              "scalar_hole45", "scalar_cell45", "scalar_residual45",
              "scalar_explicit_limit45", "scalar_edge45", "scalar_unsigned_edge45",
              "typed_inner45", "typed_abort45", "typed_rethrow45",
              "typed_swallow_zero45", "typed_nested_swallow45",
              "unknown_before_abort45", "unknown_before_return45",
              "scalar_upper_endpoint45", "scalar_unsigned_endpoint45"]
    safe = {"normal_division_safe", "caught_division_safe", "normal_bounds_safe",
            "caught_bounds_safe", "rethrow_pointer_safe", "memory_effects_safe",
            "class_normal_division_safe", "class_caught_division_safe",
            "affine_chain_division_safe", "modular_bounds_safe44",
            "typed_abort_division_safe45", "typed_rethrow_division_safe45",
            "unknown_before_abort_division_safe45"}
    unsafe = {"swallowed_keeps_warning", "caught_zero_keeps_warning", "unknown_exception_keeps_warning",
              "unknown_argument_keeps_warning", "unknown_memory_pointer_keeps_warning",
              "null_memory_pointer_keeps_error", "offset_memory_pointer_keeps_error",
              "mismatched_memory_pointer_keeps_error", "class_zero_keeps_warning",
              "affine_shift_keeps_zero", "modular_wrapped_product_bounds_keeps_warning44",
              "typed_swallow_zero_keeps_warning45", "typed_nested_swallow_keeps_warning45",
              "unknown_before_return_keeps_warning45"}
    overflow = {"affine_overflow_before_call43", "affine_overflow_in_callee43"}
    with tempfile.TemporaryDirectory(prefix="ikos-exception-regression-") as folder:
        folder = pathlib.Path(folder)
        bc, pp = folder / "input.bc", folder / "input.pp.bc"
        command([opts.clang, "-c", "-emit-llvm", "-g", "-O0", "-std=c++17",
                 "-Xclang", "-disable-O0-optnone", str(source), "-o", str(bc)])
        entries = guards + sorted(safe | unsafe | overflow)
        command([opts.ikos_pp, "-opt=basic", "-entry-points=" + ",".join(entries), str(bc), "-o", str(pp)])
        trace = command([opts.ikos_analyzer, "-a=dbz", "-d=interval", "-proc=inter",
                         "-entry-points=" + ",".join(guards), "--trace-ar-stmts", "--progress=no",
                         "-o", str(folder / "guards.db"), str(pp)])
        expected = {
            "positive31": (lambda n: n > 0, lambda n: n <= 0),
            "normal32": (lambda n: 0 <= n < 10, lambda n: n < 0 or n >= 10),
            "exception33": (lambda n: n <= 0, lambda n: n > 0),
            "exception34": (lambda n: n < 0 or n >= 10, lambda n: 0 <= n < 10),
            "swallowed35": (lambda n: True, lambda n: False),
            "split40": (lambda n: n > 0, lambda n: n < 0),
            "ordinary_wrapper31": (lambda n: n > 0, lambda n: n <= 0),
            "catch_and_rethrow": (lambda n: n >= -3, lambda n: n < -3),
            "class41": (lambda n: 0 < n <= 500, lambda n: n > 500),
            "user_class41": (lambda n: n <= 500, lambda n: n > 500),
            "constructor_throws41": (lambda n: n <= 500, lambda n: n > 500),
            "constructor_aborts41": (lambda n: n <= 500, lambda n: 500 < n <= 700),
            "unknown_cleanup41": (lambda n: n <= 500, lambda n: n > 500),
            "catch_aborting_cleanup41": (lambda n: n <= 500, lambda n: False),
            "catch_std_class41": (lambda n: n > 0, lambda n: False),
            "rethrow_std_class41": (lambda n: 0 < n <= 500, lambda n: n > 500),
            "scalar_strict45": (lambda n: 0 < n <= 300, lambda n: n < 0 or n > 300),
            "scalar_hole45": (lambda n: -5 <= n <= 5 and n != 0,
                              lambda n: n < -5 or n > 5 or n == 0),
        }
        for name, predicates in expected.items():
            for flow, predicate in zip(("normal", "exception"), predicates):
                formula, level = component(trace, name, flow)
                assert level == "Complete", (name, flow, level, formula)
                for n in [-2147483648, *range(-20, 21), 499, 500, 501, 700, 701, 2147483647]:
                    assert evaluate(formula, n) == predicate(n), (name, flow, formula, n)
            print(name + ": normal/exception formulas verified")
        minimum, maximum = -(1 << 63), (1 << 63) - 1

        def mid_exit(n, bias):
            s0, s1, s2 = n + bias, n * 3, n * 2
            s3 = s1 - s2
            t = s3 + bias
            s4 = t - s0
            v = s3 + s4
            defined = all(minimum <= x <= maximum for x in (s0, s1, s2, s3, t, s4, v))
            return defined, (0 < v <= 200, v > 200)

        samples = [minimum, minimum // 3, -201, -1, 0, 1, 199, 200, 201,
                   maximum // 3, maximum // 3 + 1, maximum // 2, maximum]
        for flow, which in (("normal", 0), ("exception", 1)):
            for name in ("affine_mid43", "affine_reordered43"):
                formula, level = component(trace, name, flow)
                assert level == "Complete", (name, flow, level, formula)
                # The guarantee concerns defined executions; arithmetic
                # safety must not be manufactured into extra input bounds.
                expected_formula = "p00 > 0 && p00 <= 200" if not which else "p00 > 200"
                assert formula == expected_formula, (name, formula)
                for n in samples:
                    for bias in samples + [maximum - n, maximum - n + 1]:
                        if minimum <= bias <= maximum:
                            defined, exits = mid_exit(n, bias)
                            if defined:
                                assert holds(formula, n, bias) == exits[which], (name, flow, formula, n, bias)
            formula, level = component(trace, "affine_outer43", flow)
            assert level == "Complete", (flow, level, formula)
            assert formula == expected_formula, formula
            for n in samples:
                defined, exits = mid_exit(n, 2 * n)
                if defined and minimum <= 2 * n <= maximum:
                    assert holds(formula, n) == exits[which], (flow, formula, n)
            formula, level = component(trace, "affine_large_coefficient43", flow)
            assert level == "Complete", (flow, level, formula)
            assert "_128" not in formula, formula
            for n in [*samples, -(1 << 31), (1 << 31) - 1, 1 << 31]:
                if -(1 << 31) <= n < (1 << 31):
                    assert evaluate(formula, n) == (not which), (flow, formula, n)
            formula, level = component(trace, "affine_cast43", flow)
            assert level == "Complete", (flow, level, formula)
            for n in [-2147483648, -201, -1, 0, 1, 200, 2147483647]:
                assert evaluate(formula, n) == (n < 0 if flow == "exception" else False), (flow, formula, n)
            formula, level = component(trace, "affine_negative_scale43", flow)
            assert level == "Complete", (flow, level, formula)
            assert formula == expected_formula, formula
            for n in [*samples, (1 << 61) - 1, 1 << 61, (1 << 61) + 1]:
                if minimum <= -4 * n <= maximum and minimum <= -3 * n <= maximum:
                    expected_value = 0 < n <= 200 if not which else n > 200
                    assert evaluate(formula, n) == expected_value, (flow, formula, n)
            formula, level = component(trace, "affine_equivalent_bounds43", flow)
            assert level == "Complete", (flow, level, formula)
            assert formula == ("true" if not which else "false"), formula
            for n in samples + [-(maximum // 3), -(maximum // 3) - 1]:
                if minimum <= 3 * n <= maximum:
                    assert evaluate(formula, n) == (not which), (flow, formula, n)
            formula, level = component(trace, "affine_explicit_limit43", flow)
            assert level == "Complete", (flow, level, formula)
            # A redundant source bound may disappear only by equivalence.
            for n in samples:
                expected_value = 0 < n <= 200 if not which else n > 200
                assert evaluate(formula, n) == expected_value, (flow, formula, n)
            formula, level = component(trace, "scalar_explicit_limit45", flow)
            assert level == "Complete" and str(maximum // 3) in formula, (flow, level, formula)
            for n in samples:
                assert evaluate(formula, n) == (n <= maximum // 3 if not which else n > maximum // 3), (flow, formula, n)
            for name in ("scalar_edge45", "scalar_unsigned_edge45"):
                formula, level = component(trace, name, flow)
                assert level == "Complete", (name, flow, level, formula)
                values = samples if name == "scalar_edge45" else [0, 1, (1 << 31), (1 << 32) - 2, (1 << 32) - 1]
                for n in values:
                    exceptional = n in (minimum, maximum) if name == "scalar_edge45" else n == (1 << 32) - 1
                    assert evaluate(formula, n) == (exceptional if which else not exceptional), (name, flow, formula, n)
            for name in ("affine_wrapped_input43", "affine_truncated_input43"):
                formula, level = component(trace, name, flow)
                assert level == "Complete", (name, flow, level, formula)
                unsigned_samples = [*range(256), (1 << 63) - 1, 1 << 63,
                                    (1 << 63) + 1, (1 << 64) - 1]
                for n in unsigned_samples:
                    if name == "affine_wrapped_input43":
                        value = (2 * n) % (1 << 64)
                        if value >= (1 << 63): value -= 1 << 64
                    else:
                        value = n % 256
                    defined = value != maximum
                    expected_value = defined and (0 < value + 1 <= 200 if not which else value + 1 > 200)
                    assert evaluate(formula, n) == expected_value, (name, flow, formula, n)
        print("affine substitution: defined executions, source guards, casts and caller composition verified")
        unsigned_max = (1 << 32) - 1
        unsigned_samples = [*range(512), (1 << 31) - 1, 1 << 31,
                            (1 << 31) + 1, *range(unsigned_max - 4, unsigned_max + 1)]
        for flow in ("normal", "exception"):
            formula, level = component(trace, "modular_coefficient44", flow)
            assert level == "Complete" and formula == ("true" if flow == "normal" else "false"), (flow, level, formula)
            for name, limit in (("modular_row44", 6), ("modular_col44", 8), ("modular_composed44", 8)):
                formula, level = component(trace, name, flow)
                expected_formula = "p00 u< " + str(limit) if flow == "normal" else "p00 u>= " + str(limit)
                assert level == "Complete" and formula == expected_formula, (name, flow, level, formula)
                for n in unsigned_samples:
                    assert evaluate(formula, n) == (n < limit if flow == "normal" else n >= limit), (name, flow, n)
            for name in ("modular_shift_guard44", "modular_wrapped_product44"):
                formula, level = component(trace, name, flow)
                assert level == "Complete", (name, flow, level, formula)
                assert ("USub_32" if name == "modular_shift_guard44" else "UMul_32") in formula, formula
                for n in unsigned_samples:
                    result = (n - 4 if name == "modular_shift_guard44" else n * 2) % (1 << 32)
                    assert evaluate(formula, n) == (result < 8 if flow == "normal" else result >= 8), (name, flow, formula, n)
            for name in ("modular_different_operands44", "modular_opaque44"):
                formula, level = component(trace, name, flow)
                assert level == "Complete", (name, flow, level, formula)
                if name == "modular_opaque44":
                    assert "URem_32" in formula and "UMul_32" not in formula, formula
                else:
                    assert "p10" in formula, formula
                for n in unsigned_samples:
                    for k in (0, 1, 4, unsigned_max):
                        normal = n % 3 == 0 if name == "modular_opaque44" else (n + 4 - k) % (1 << 32) < 8
                        assert holds(formula, n, k) == (normal if flow == "normal" else not normal), (name, flow, formula, n, k)
        print("modular simplification: cancellation, caller substitution, wrapping boundaries and opaque atoms verified")
        formula, level = component(trace, "scalar_strict45", "exception")
        assert level == "Complete" and formula in ("(p00 > 300) || (p00 < 0)", "(p00 < 0) || (p00 > 300)"), formula
        for flow in ("normal", "exception"):
            formula, level = component(trace, "scalar_cell45", flow)
            assert level == "Complete", (flow, level, formula)
            if flow == "exception":
                assert formula in ("(p00 u>= 6) || (p10 u>= 8)", "(p10 u>= 8) || (p00 u>= 6)"), formula
            for n in [0, 5, 6, 7, (1 << 32) - 1]:
                for k in [0, 7, 8, (1 << 32) - 1]:
                    normal = n < 6 and k < 8
                    assert holds(formula, n, k) == (normal if flow == "normal" else not normal), (flow, formula, n, k)
            formula, level = component(trace, "scalar_residual45", flow)
            assert level == "Complete", (flow, level, formula)
            for n in [-1, 0, 5, 6, 7, 2147483647]:
                for k in [-1, 0, 1, 7, 8, 2147483647]:
                    exceptional = (n >= 6 and k == 0) or (n < 6 and k >= 8)
                    assert holds(formula, n, k) == (exceptional if flow == "exception" else not exceptional), (flow, formula, n, k)
        print("scalar logic: redundant bounds, OR absorption, NE holes, residual guards and finite endpoints verified")
        for name, expected_formula in (("scalar_strict45", "p00 > 0 && p00 <= 300"),
                                       ("scalar_upper_endpoint45", "p00 < 0"),
                                       ("scalar_unsigned_endpoint45", "p00 u< 10")):
            assert component(trace, name, "normal") == (expected_formula, "Complete"), name
        for name in ("typed_abort45", "typed_rethrow45"):
            formula, level = component(trace, name, "normal")
            assert (formula, level) == ("p00 > 0 && p00 <= 300", "Complete"), (name, formula, level)
            assert component(trace, name, "exception")[1] == "Partial", name
            for n in [-32768, -1, 0, 1, 299, 300, 301, 32767]:
                assert evaluate(formula, n) == (0 < n <= 300), (name, formula, n)
        assert component(trace, "unknown_before_abort45", "normal") == ("p00 > 0", "Complete")
        assert component(trace, "unknown_before_abort45", "exception")[1] == "Partial"
        for name in ("typed_swallow_zero45", "typed_nested_swallow45", "unknown_before_return45"):
            assert component(trace, name, "normal")[1] == "Partial", name
        print("normal coverage: nonreturning unsupported suffixes certified; returning handlers stay partial")
        for name in ("pointer36", "rethrow36"):
            for flow, atom in (("normal", "NonNull(p00)"), ("exception", "IsNull(p00)")):
                formula, level = component(trace, name, flow)
                assert level == "Complete" and formula == atom, (name, flow, level, formula)
        for name in ("memory37", "memory_wrapper37", "local_memory37"):
            block = trace.split("Function: " + name + "\n", 1)[1].split("Function: ", 1)[0]
            required = "Writable(p10, si32, 4 bytes, align 4)"
            assert (required in block) == (name != "local_memory37"), (name, block)
            for flow, truth in (("normal", False), ("exception", True)):
                formula, level = component(trace, name, flow)
                assert level == "Complete", (name, flow, level, formula)
                for n in (0, 1):
                    assert evaluate(formula, n) == (bool(n) == truth), (name, flow, formula, n)
            if name == "memory37":
                normal = block.split("mu_normal_exit (caller-visible):", 1)[1].split("coverage:", 1)[0]
                exceptional = block.split("mu_exception_exit (caller-visible):", 1)[1].split("coverage:", 1)[0]
                assert "-> 10" in normal and "-> 5" in exceptional, (normal, exceptional)
        print("conditional pointer writes: formulas, memory effects and precondition propagation verified")
        formula, level = component(trace, "mixed39", "exception")
        assert level == "Partial", (level, formula)
        for n in range(-3, 4): assert evaluate(formula, n) == (n > 0), (formula, n)
        for name in ("typed_catch_fallback", "mismatched_memory37", "volatile_memory37", "external_load37"):
            for flow in ("normal", "exception"):
                assert component(trace, name, flow)[1] == "Partial", (name, flow)
        print("unknown calls, unsupported catch/load and mismatched/volatile memory: fallback verified")
        for name in ("unknown_constructor41", "unknown_message41"):
            assert component(trace, name, "exception")[1] == "Partial", name
            expected_normal = "p00 <= 500" if name == "unknown_constructor41" else "false"
            assert component(trace, name, "normal") == (expected_normal, "Complete"), name
        for flow in ("normal", "exception"):
            assert component(trace, "catch_unknown_cleanup41", flow)[1] == "Partial", flow
        formula, level = component(trace, "nondeterministic_constructor41", "exception")
        assert (formula, level) == ("false", "Partial"), (formula, level)
        # On this little-endian target the overlapping byte write clears code
        # and aborts. A stale 32-bit value must never certify the later throw.
        assert component(trace, "overlapping_class41", "exception")[0] == "false"
        print("class exceptions: unknown constructors/cleanup and mixed nondeterministic exits stay partial")
        precondition_entries = {name for name in safe | unsafe if "memory_pointer" in name} | {"memory_effects_safe"}
        runs = [(entry, 1) for entry in sorted(safe | unsafe)]
        runs += [(entry, 2) for entry in sorted(precondition_entries)]
        for entry, jobs in runs:
            db = folder / (entry + "-j" + str(jobs) + ".db")
            analysis = "boa" if "bounds" in entry or entry.startswith(("offset_memory", "mismatched_memory")) else "nullity" if "pointer" in entry else "dbz"
            options = ["--trace-ar-stmts"] if entry in precondition_entries else []
            entry_trace = command([opts.ikos_analyzer, "-a=" + analysis, "-d=interval", "-proc=inter",
                                   "-j=" + str(jobs), "-entry-points=" + entry, "--progress=no", "-o", str(db), *options, str(pp)])
            if options:
                decision = "proved at call entry" if entry == "memory_effects_safe" else "not proved; skipped"
                assert "Summary memory preconditions [memory37]: " + decision in entry_trace, (entry, decision)
            with sqlite3.connect(db) as connection:
                counts = dict(connection.execute("SELECT status, COUNT(*) FROM checks GROUP BY status"))
            problems = counts.get(1, 0) + counts.get(2, 0)
            assert (problems == 0 and counts.get(0, 0) > 0) if entry in safe else problems > 0, (entry, counts)
            if entry.endswith("keeps_error"):
                assert counts.get(2, 0) > 0, counts
            print(entry + " (j=" + str(jobs) + "): checker result verified")
        for entry in sorted(overflow):
            db = folder / (entry + "-sio.db")
            command([opts.ikos_analyzer, "-a=sio", "-d=interval", "-proc=inter",
                     "-entry-points=" + entry, "--progress=no", "-o", str(db), str(pp)])
            with sqlite3.connect(db) as connection:
                counts = dict(connection.execute("SELECT status, COUNT(*) FROM checks GROUP BY status"))
            assert counts.get(1, 0) + counts.get(2, 0) > 0, (entry, counts)
            if entry == "affine_overflow_in_callee43":
                assert counts.get(2, 0) > 0, (entry, counts)
            print(entry + ": native overflow diagnostic preserved")
        # A visible replacement allocator can terminate during message
        # construction. Do not reuse the default library outcome contract.
        replacement = folder / "replacement.cpp"
        replacement.write_text('''#include <cstdlib>
#include <stdexcept>
void* operator new(std::size_t) { std::abort(); }
extern "C" void replaced_allocation(int n) {
  if (n > 500) throw std::runtime_error("message");
}
''')
        command([opts.clang, "-c", "-emit-llvm", "-g", "-O0", "-std=c++17",
                 "-Xclang", "-disable-O0-optnone", str(replacement), "-o", str(bc)])
        command([opts.ikos_pp, "-opt=basic", "-entry-points=replaced_allocation", str(bc), "-o", str(pp)])
        trace = command([opts.ikos_analyzer, "-a=dbz", "-d=interval", "-proc=inter",
                         "-entry-points=replaced_allocation", "--trace-ar-stmts", "--progress=no",
                         "-o", str(folder / "replacement.db"), str(pp)])
        assert component(trace, "replaced_allocation", "exception") == ("false", "Partial")
        print("replacement allocator: standard constructor model disabled")


if __name__ == "__main__":
    main()
