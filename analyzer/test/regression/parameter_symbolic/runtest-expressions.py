#!/usr/bin/env python3
"""Check expression reuse and the denotation of retained exit conditions."""

import argparse
import pathlib
import re
import sqlite3
import subprocess
import tempfile


def command(args):
    result = subprocess.run(args, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    if result.returncode:
        raise AssertionError("Command failed: %r\n%s" % (args, result.stdout))
    return result.stdout


def summary(trace, function):
    marker = "Function: " + function + "\n"
    assert marker in trace, function
    block = trace.split(marker, 1)[1].split("Function: ", 1)[0]
    normal = block.split("[exception_exit]", 1)[0]
    assert "coverage: Complete" in normal, (function, normal)
    text = normal.split("C_normal (parameter constraints):\n", 1)[1]
    lines = []
    for line in text.splitlines():
        if not line.startswith("    "):
            break
        lines.append(line.strip())
    return " ".join(lines)


def holds(condition, *values):
    # Parse the small, typed formula language emitted by these fixtures. No
    # arbitrary Python syntax is accepted from analyzer output.
    tokens = re.findall(r"u?(?:==|!=|>=|<=|>|<)|[A-Za-z_][A-Za-z_0-9]*|[0-9]+|&&|\|\||[(),-]", condition)
    position = 0

    def take(token=None):
        nonlocal position
        item = tokens[position]
        if token is not None:
            assert item == token, (condition, token, item)
        position += 1
        return item

    def value():
        token = take()
        if token == "-":
            return -value()
        if token.isdigit():
            return int(token)
        if re.fullmatch(r"p[0-9]+0", token):
            return values[int(token[1:-1])]
        match = re.fullmatch(r"([SU])(Add|Sub|Mul|Div|Rem|Cast)_([0-9]+)", token)
        assert match, (condition, token)
        sign, operation, width = match.group(1), match.group(2), int(match.group(3))
        take("(")
        left = value()
        if operation == "Cast":
            result = left
        else:
            take(",")
            right = value()
            if operation == "Add": result = left + right
            elif operation == "Sub": result = left - right
            elif operation == "Mul": result = left * right
            else:
                quotient = abs(left) // abs(right)
                if (left < 0) != (right < 0): quotient = -quotient
                result = quotient if operation == "Div" else left - quotient * right
        take(")")
        result %= 1 << width
        if sign == "S" and result >= 1 << (width - 1): result -= 1 << width
        return result

    def atom():
        if tokens[position] == "(":
            take("("); result = disjunction(); take(")"); return result
        left = value(); op = take().removeprefix("u"); right = value()
        return {"==": left == right, "!=": left != right,
                ">=": left >= right, "<=": left <= right,
                ">": left > right, "<": left < right}[op]

    def conjunction():
        result = atom()
        while position < len(tokens) and tokens[position] == "&&":
            take(); rhs = atom(); result = result and rhs
        return result

    def disjunction():
        result = conjunction()
        while position < len(tokens) and tokens[position] == "||":
            take(); rhs = conjunction(); result = result or rhs
        return result

    result = disjunction()
    assert position == len(tokens), (condition, tokens[position:])
    return result


def main():
    parser = argparse.ArgumentParser()
    for tool in ("clang", "ikos-pp", "ikos-analyzer"):
        parser.add_argument("--" + tool, required=True)
    opts = parser.parse_args()
    source = pathlib.Path(__file__).with_name("expressions.cpp")
    with tempfile.TemporaryDirectory(prefix="ikos-expression-regression-") as folder:
        folder = pathlib.Path(folder)
        bc, pp = folder / "input.bc", folder / "input.pp.bc"
        command([opts.clang, "-c", "-emit-llvm", "-g", "-O0", "-std=c++17",
                 "-Xclang", "-disable-O0-optnone", str(source), "-o", str(bc)])
        entries = ["collect_summaries", "repeated_sum", "wrapped_sum", "unguarded_join",
                   "changed_operand", "different_unknown", "loop_redefinition",
                   "partial_keeps_zero", "partial_exception_keeps_error",
                   "positive_division", "bounded_index", "bounded_product"]
        backward_safe = {"backward_subtract", "backward_constant_minus_variable",
                         "backward_division", "backward_bounded_product",
                         "backward_bounded_difference"}
        entries += sorted(backward_safe) + ["backward_add_keeps_zero",
                   "backward_unknown_difference", "backward_wrapped_product_keeps_large",
                   "backward_truncation_keeps_high_bits"]
        command([opts.ikos_pp, "-opt=basic", "-entry-points=" + ",".join(entries),
                 str(bc), "-o", str(pp)])
        safe = {"repeated_sum", "wrapped_sum", "positive_division", "bounded_index", "bounded_product"}
        safe |= backward_safe
        for entry in entries:
            db = folder / (entry + ".db")
            analyses = "boa" if entry in {"bounded_index", "bounded_product"} else "dbz"
            args = [opts.ikos_analyzer, "-a=" + analyses, "-d=interval", "-proc=inter",
                    "-entry-points=" + entry, "--progress=no", "-o", str(db), str(pp)]
            if entry == "collect_summaries": args += ["--trace-ar-stmts"]
            trace = command(args)
            if entry == "collect_summaries":
                formulas = {name: summary(trace, name) for name in
                            ("range21", "division23", "remainder24", "index29",
                             "index29_strict", "product30", "product30_strict")}
                assert "SDiv_64" in formulas["division23"]
                for name in ("range21", "remainder24"): assert "SRem_64" in formulas[name]
                for name in ("index29", "index29_strict"):
                    assert "URem_" in formulas[name] and "USub_" in formulas[name] and "||" in formulas[name]
                for name in ("product30", "product30_strict"): assert "UMul_32" in formulas[name]
                for n in range(-3, 2003):
                    assert holds(formulas["range21"], n) == (1 <= n <= 1000 and (n % 2 == 0 or n <= 500)), n
                    assert holds(formulas["division23"], n) == (2 <= n <= 1000), n
                    assert holds(formulas["remainder24"], n) == (0 < n <= 2000 and (n % 2 == 0 or n <= 100)), n
                for i in range(16):
                    for name, limit in (("index29", 2), ("index29_strict", 4)):
                        aligned = i if i % 2 == 0 else (i - 1) % (1 << 64)
                        assert holds(formulas[name], i) == (2 <= i < 8 and aligned >= limit), (name, i)
                for s in range(8):
                    for i in range(8):
                        for name, limit in (("product30", 9), ("product30_strict", 4)):
                            assert holds(formulas[name], s, i) == (0 < s <= 3 and i <= 3 and s * i <= limit), (name, s, i)
                print("typed exit formulas and stricter guard variants: passed")
                continue
            with sqlite3.connect(db) as connection:
                counts = dict(connection.execute("SELECT status, COUNT(*) FROM checks GROUP BY status"))
            problems = counts.get(1, 0) + counts.get(2, 0)
            assert (problems == 0) if entry in safe else (problems > 0), (entry, counts)
            assert counts.get(0, 0) > 0 if entry in safe else True, (entry, counts)
            print(entry + ": passed")


if __name__ == "__main__":
    main()
