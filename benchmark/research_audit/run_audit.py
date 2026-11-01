#!/usr/bin/env python3
"""Audit flow summaries and source-line checker results; never edit the analyzer."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import itertools
import json
from pathlib import Path
import re
import runpy
import sqlite3
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = HERE / "flow_matrix.cpp"
HELPERS = runpy.run_path(str(ROOT / "analyzer/test/regression/parameter_symbolic/runtest-exceptions.py"))

CASES = [
    ("audit_dbz_i8", "dbz", "dbz8_n dbz8_e", True),
    ("audit_typed_rethrow", "boa", "typed_n typed_e", True),
    ("audit_dbz_i16", "dbz", "dbz_n dbz_e", True),
    ("audit_shift_i16", "shc", "shift_n shift_e", True),
    ("audit_sio_i32", "sio", "sio_n sio_e", True),
    ("audit_uio_u8", "uio", "uio_n uio_e", True),
    ("audit_boa_sum64", "boa", "sum_n sum_e", True),
    ("audit_prover_sum64", "prover", "prover_n prover_e", True),
    ("audit_boa_div64", "boa", "quotient_n quotient_e", True),
    ("audit_dbz_rem16", "dbz", "rem_n rem_e", True),
    ("audit_nullity", "nullity", "pointer_n pointer_e", True),
    ("audit_bool_origin", "prover", "bool_n bool_e", True),
    ("audit_bool_origin_phi", "prover", "bool_phi_n bool_phi_e", False),
    ("audit_class_bounds", "boa", "class_n class_e", True),
    ("audit_standard_class", "boa", "stdclass_n stdclass_e", True),
    ("audit_destructor_invalidation", "uva,dbz", "destructor_effect", False),
    ("audit_destructor_program_exit", "dbz", "destructor_unreachable", False),
    ("audit_memory", "uva,dbz", "memory_n memory_e", True),
    ("audit_wrapped_product", "boa", "product_n", True),
    ("audit_ne_hole", "dbz", "ne_hole", False),
    ("audit_partial", "dbz", "partial_n partial_e", False),
    ("audit_memory_invalidation", "uva,dbz", "invalidated_memory", False),
    ("audit_redefinition", "boa", "redefined_n redefined_e", False),
    ("audit_truncation", "boa", "truncation_n truncation_e", False),
    ("audit_overflow_before_guard", "sio", "overflow_before", False),
    ("audit_float_probe", "dbz", "float_n float_e", False),
    ("audit_packet", "boa,dbz,nullity,uva,sio,shc,prover", " ".join(
        f"packet_{kind}_{flow}" for flow in ("n", "e")
        for kind in ("div", "row", "sum", "pointer", "shift", "sio", "effect")), False),
]

EXPECTED_GUARDS = {
    "audit_dbz_i8": "split_i8",
    "audit_dbz_i16": "split_i16", "audit_shift_i16": "split_i16",
    "audit_sio_i32": "split_i32", "audit_uio_u8": "split_u8",
    "audit_boa_sum64": "sum_wrapper64", "audit_prover_sum64": "sum_wrapper64",
    "audit_boa_div64": "split_quotient64", "audit_dbz_rem16": "split_remainder16",
    "audit_nullity": "split_pointers", "audit_bool_origin": "split_null_bool",
    "audit_bool_origin_phi": "split_null_bool", "audit_memory": "split_memory",
    "audit_wrapped_product": "split_product32", "audit_class_bounds": "class_wrapper",
    "audit_standard_class": "split_standard_class", "audit_packet": "packet_wrapper",
}
PROGRAM_GUARDS = {"split_i32", "split_u8", "split_quotient64", "split_remainder16",
                  "split_pointers", "split_memory", "split_product32", "split_standard_class"}


def evaluate(formula, values):
    if formula in ("true", "false"):
        return formula == "true"
    formula = re.sub(r"NonNull\((p\d+0)\)", r"\1 != 0", formula)
    formula = re.sub(r"IsNull\((p\d+0)\)", r"\1 == 0", formula)
    return HELPERS["holds"](formula, *values)


def required_checkers(target):
    # CheckerName enum IDs from analyzer/include/ikos/analyzer/checker/name.hpp.
    # Require the intended checker, not merely another safe check on this line.
    if target.startswith(("memory_", "invalidated_memory", "destructor_effect")):
        return {1, 5}  # dbz, uva
    if target.startswith(("dbz", "rem_", "ne_", "partial_", "float_", "packet_div_", "destructor_unreachable")):
        return {1}
    if target.startswith(("shift_", "packet_shift_")):
        return {8}
    if target.startswith(("sio_", "overflow_", "packet_sio_")):
        return {6}
    if target.startswith("uio_"):
        return {7}
    if target.startswith(("prover_", "bool_", "packet_effect_")):
        return {3}
    if target.startswith(("pointer_", "packet_pointer_")):
        return {2}
    return {0}  # boa


def check_formulas(trace, entry):
    """Independent concrete predicates; finite boundary sampling, not a proof."""
    specifications = {
        "split_i32": ([(n,) for n in (-2147483648, -9, *range(-8, 9), 2147483647)],
                      lambda n: 0 <= n <= 7, lambda n: -8 <= n < 0),
        "split_i16": ([(n,) for n in (-32768, -9, *range(-8, 9), 32767)],
                      lambda n: 0 <= n <= 7, lambda n: -8 <= n < 0),
        "split_i8": ([(n,) for n in range(-128, 128)],
                     lambda n: 0 <= n <= 7, lambda n: -8 <= n < 0),
        "class_wrapper": ([(n,) for n in (-2147483648, -9, *range(-8, 9), 2147483647)],
                          lambda n: 0 <= n <= 7, lambda n: -8 <= n < 0),
        "split_standard_class": ([(n,) for n in (-2147483648, -9, *range(-8, 9), 2147483647)],
                                 lambda n: 0 <= n <= 7, lambda n: -8 <= n < 0),
        "split_u8": ([(n,) for n in range(256)], lambda n: 1 <= n <= 4, lambda n: 5 <= n <= 8),
        "sum_wrapper64": ([(n,) for n in (*range(33), (1 << 64) - 1)],
                          lambda n: n < 8, lambda n: 8 <= n < 16),
        "split_quotient64": ([(n,) for n in (-(1 << 63), *range(-2, 67), (1 << 63) - 1)],
                             lambda n: 0 <= n < 32, lambda n: 32 <= n <= 63),
        "split_remainder16": ([(n,) for n in (-32768, *range(-2, 33), 32767)],
                              lambda n: 1 <= n <= 30 and n % 3 != 0,
                              lambda n: 1 <= n <= 30 and n % 3 == 0),
        "split_pointers": (list(itertools.product((0, 1), repeat=2)),
                           lambda p, b: p != 0 and b == 0, lambda p, b: p == 0 and b != 0),
        "split_null_bool": ([(0,), (1,)], lambda absent: not absent, lambda absent: bool(absent)),
        "split_memory": ([(-1, 1), (0, 1), (1, 1), (2, 1)],
                         lambda mode, out: mode == 0, lambda mode, out: mode == 1),
        "split_product32": (list(itertools.product((0, 1, 2, 3, 4, 5, (1 << 32) - 1),
                                                   (0, 1, 3, 4, 15, 16, 1 << 30, 1 << 31, (1 << 32) - 1))),
                            lambda s, i: 1 <= s <= 4 and s * i % (1 << 32) < 16,
                            lambda s, i: 1 <= s <= 4 and s * i % (1 << 32) >= 16),
        "packet_wrapper": (list(itertools.product((-9, -8, -1, 0, 7, 8),
                                                   (0, 7, 8, 15, 16, (1 << 64) - 1),
                                                   (0, 3, 4, 7, 8), (0, 1), (0, 1), (1,))),
                           lambda n, amount, row, p, b, out: 0 <= n <= 7 and amount < 8 and row < 4 and p != 0 and b == 0,
                           lambda n, amount, row, p, b, out: -8 <= n < 0 and 8 <= amount < 16 and 4 <= row < 8 and p == 0 and b != 0),
    }
    results = []
    required = EXPECTED_GUARDS.get(entry)
    if required and "Function: " + required + "\n" not in trace:
        results.append(dict(function=required, flow="both", issue="required summary missing"))
    for name, (samples, normal, exception) in specifications.items():
        if "Function: " + name + "\n" not in trace:
            continue
        for flow, predicate in (("normal", normal), ("exception", exception)):
            formula, coverage = HELPERS["component"](trace, name, flow)
            result = {"function": name, "flow": flow, "formula": formula, "coverage": coverage}
            if coverage != "Complete":
                result["issue"] = "expected Complete"
            else:
                for sample in samples:
                    try:
                        actual = evaluate(formula, sample)
                    except Exception as error:
                        result["issue"] = "formula evaluation failed: " + repr(error)
                        break
                    expected = bool(predicate(*sample))
                    if actual != expected:
                        result.update(issue="formula mismatch", witness=sample, expected=expected, actual=actual)
                        break
            results.append(result)
        if name in PROGRAM_GUARDS:
            block = trace.split("Function: " + name + "\n", 1)[1].split("Function: ", 1)[0]
            present = re.search(r"^\[program_exit\] (.*)$", block, re.M)
            program = dict(function=name, flow="program", local_cfg=present[1] if present else None)
            if not present or present[1].startswith("none"):
                program["issue"] = "expected local program-exit CFG"
            results.append(program)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ikos", type=Path, default=ROOT / "install/bin/ikos")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--workers", type=int, default=3)
    args = parser.parse_args()
    output = args.output_dir or Path(tempfile.mkdtemp(prefix="ikos-research-audit-"))
    output.mkdir(parents=True, exist_ok=True)
    markers = {}
    for line, text in enumerate(SOURCE.read_text().splitlines(), 1):
        if match := re.search(r"// AUDIT: (\w+) (\w+)", text):
            markers[match[1]] = (line, match[2])
    jobs = []
    for case in CASES:
        jobs.append((case, "base"))
        if case[3]:
            jobs.append((case, "bad_normal"))
            if any(t.endswith("_e") for t in case[2].split()):
                jobs.append((case, "bad_exception"))

    def run(job):
        (entry, analyses, targets, _), variant = job
        name = entry + "-" + variant
        db, log = output / (name + ".db"), output / (name + ".log")
        command = [str(args.ikos), str(SOURCE), "--entry-points=" + entry,
                   "--analyses=" + analyses, "--domain=interval", "--trace-ar-stmts", "-o", str(db)]
        if variant != "base":
            command += ["-D", variant.upper() + "=1"]
        with log.open("w") as stream:
            process = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
        record = {"entry": entry, "variant": variant, "analyses": analyses, "log": str(log), "db": str(db)}
        if process.returncode:
            record["error"] = "analysis failed: exit " + str(process.returncode)
            return record
        trace = log.read_text()
        with sqlite3.connect(db) as connection:
            checks = connection.execute(
                "SELECT s.line,c.status,c.checker,c.kind FROM checks c "
                "JOIN statements s ON c.statement_id=s.id "
                "JOIN files f ON s.file_id=f.id WHERE f.path=?", (str(SOURCE),)).fetchall()
        record["targets"] = []
        for target in targets.split():
            line, expectation = markers[target]
            if variant == "bad_normal" and target.endswith("_n"):
                expectation = "detect"
            if variant == "bad_exception" and target.endswith("_e"):
                expectation = "detect"
            relevant = [dict(status=status, checker=checker, kind=kind) for location, status, checker, kind in checks if location == line]
            required = required_checkers(target)
            intended = [check for check in relevant if check["checker"] in required]
            statuses = {check["status"] for check in intended}
            missing = required - {check["checker"] for check in intended}
            if expectation == "unsupported":
                outcome = "UNSUPPORTED" if not relevant else "OBSERVED"
            elif expectation == "limitation":
                outcome = "KNOWN_LIMIT" if 1 in statuses or 2 in statuses else "PASS" if statuses == {0} else "FAIL"
            elif expectation == "detect":
                outcome = "PASS" if 1 in statuses or 2 in statuses else "FAIL"
            elif expectation == "unreachable":
                outcome = "PASS" if statuses == {3} and not missing else "FAIL"
            else:
                outcome = "PASS" if statuses == {0} and not missing else "FAIL"
            if missing and expectation not in ("unsupported",):
                outcome = "FAIL"
            record["targets"].append(dict(target=target, line=line, expectation=expectation, outcome=outcome,
                                          checks=intended, other_checks=[c for c in relevant if c not in intended],
                                          missing_checkers=sorted(missing)))
        record["summaries"] = check_formulas(trace, entry) if variant == "base" else []
        record["partial_coverages"] = {}
        for function in ("unknown_outcome", "split_float"):
            if "Function: " + function + "\n" in trace:
                record["partial_coverages"][function] = {
                    flow: HELPERS["component"](trace, function, flow) for flow in ("normal", "exception")}
                for flow, (_, coverage) in record["partial_coverages"][function].items():
                    if coverage != "Partial":
                        record["summaries"].append(dict(function=function, flow=flow, issue="unsupported flow unexpectedly Complete"))
        record["entry_coverages"] = {
            flow: HELPERS["component"](trace, entry, flow) for flow in ("normal", "exception")}
        if "Function: typed_wrapper\n" in trace:
            record["typed_wrapper_coverages"] = {
                flow: HELPERS["component"](trace, "typed_wrapper", flow) for flow in ("normal", "exception")}
        return record

    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        results = list(pool.map(run, jobs))
    (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    lines = ["# Research flow audit", "", "Finite checker/summary checks; this is not a proof of completeness.", "",
             "| Entry | Variant | Target results | Summary issues |", "|---|---|---|---|"]
    failures = 0
    for result in results:
        issues = [r for r in result.get("summaries", []) if "issue" in r]
        failures += bool(result.get("error")) + len(issues)
        failures += sum(t["outcome"] == "FAIL" for t in result.get("targets", []))
        status = result.get("error") or ", ".join(t["target"] + ": " + t["outcome"] for t in result["targets"])
        lines.append(f'| {result["entry"]} | {result["variant"]} | {status} | {len(issues)} |')
    lines += ["", f"Runs: {len(results)}. Unexpected target/summary outcomes: {failures}.",
              "", "Raw source-line check records, sampled counterexamples and exact formulas are in results.json; logs and databases are retained alongside it."]
    (output / "report.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print("\nOutput:", output)
    # Fail closed on missing/unreachable checks, tool errors or unproved safe targets.
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
