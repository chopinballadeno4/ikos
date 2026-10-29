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
              "volatile_memory37", "external_load37"]
    safe = {"normal_division_safe", "caught_division_safe", "normal_bounds_safe",
            "caught_bounds_safe", "rethrow_pointer_safe", "memory_effects_safe"}
    unsafe = {"swallowed_keeps_warning", "caught_zero_keeps_warning", "unknown_exception_keeps_warning",
              "unknown_argument_keeps_warning", "unknown_memory_pointer_keeps_warning",
              "null_memory_pointer_keeps_error", "offset_memory_pointer_keeps_error",
              "mismatched_memory_pointer_keeps_error"}
    with tempfile.TemporaryDirectory(prefix="ikos-exception-regression-") as folder:
        folder = pathlib.Path(folder)
        bc, pp = folder / "input.bc", folder / "input.pp.bc"
        command([opts.clang, "-c", "-emit-llvm", "-g", "-O0", "-std=c++17",
                 "-Xclang", "-disable-O0-optnone", str(source), "-o", str(bc)])
        entries = guards + sorted(safe | unsafe)
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
        }
        for name, predicates in expected.items():
            for flow, predicate in zip(("normal", "exception"), predicates):
                formula, level = component(trace, name, flow)
                assert level == "Complete", (name, flow, level, formula)
                for n in [-2147483648, *range(-20, 21), 2147483647]:
                    assert evaluate(formula, n) == predicate(n), (name, flow, formula, n)
            print(name + ": normal/exception formulas verified")
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


if __name__ == "__main__":
    main()
