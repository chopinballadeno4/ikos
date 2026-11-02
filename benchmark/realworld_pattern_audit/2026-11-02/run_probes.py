#!/usr/bin/env python3
"""Use actual project headers/bodies. No project or analyzer sources are edited.

The rclcpp case extracts one exact function body because ROS 2 is unavailable.
An opaque enum and its real external operator declaration preserve unknown
behavior; this is explicitly a fragment test, not a ROS build.
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse
import hashlib
import json
import os
import re
import signal
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SOURCE = REPO.parent / 'realworld'
LLVM = Path('/opt/homebrew/opt/llvm@14/bin')
TOOLS = {'original': '/Users/baesungmin/Projects/ikos/install/bin/ikos',
         'dev': '/Users/baesungmin/.local/bin/ikos-dev'}


def run(command, log, timeout=120):
    start = time.monotonic()
    with log.open('w') as out:
        p = subprocess.Popen([str(c) for c in command], stdout=out,
                             stderr=subprocess.STDOUT, start_new_session=True)
        try:
            rc = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGTERM)
            try:
                p.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(p.pid, signal.SIGKILL)
                p.wait()
            rc = 124
    result = {'command': list(map(str, command)), 'rc': rc,
              'seconds': round(time.monotonic() - start, 3), 'log': str(log)}
    log.with_suffix('.result.json').write_text(json.dumps(result, indent=2))
    return result


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cache', type=Path,
                        default=Path('/private/tmp/ikos-realworld-20261101-pou0ms1d'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--case', action='append')
    parser.add_argument('--entry', action='append')
    args = parser.parse_args()
    out = args.output or Path(tempfile.mkdtemp(prefix='ikos-pattern-audit-', dir='/private/tmp'))
    out.mkdir(parents=True, exist_ok=True)
    print('OUTPUT', out, flush=True)
    (HERE / 'last-output.txt').write_text(str(out) + '\n')
    common = [LLVM / 'clang++', '-std=c++17', '-D__IKOS__', '-g', '-O0', '-emit-llvm', '-c',
              '-Xclang', '-disable-O0-optnone', '-fno-discard-value-names',
              '-fno-rtti', '-I' + str(REPO / 'analyzer/include')]
    generated_include = out/'generated-include'
    generated_include.mkdir(exist_ok=True)
    # The integer Matrix accessor under test uses no board configuration macro.
    # This is a header-only diagnostic, not a firmware or real board build.
    (generated_include/'px4_boardconfig.h').write_text(
        '// No board-specific configuration for the isolated integer Matrix probe.\n')
    px4_includes = [SOURCE/'PX4-Autopilot/src/lib/matrix',
                    SOURCE/'PX4-Autopilot/platforms/common/include', generated_include]
    cases = {
        'fmt': ('fmt_probe.cpp', [SOURCE/'fmt/include'], [], ['probe_normal']),
        'fmt-declaration': ('fmt_probe.cpp', [SOURCE/'fmt/include'], [], ['probe_normal']),
        'fmt-ndebug': ('fmt_probe.cpp', [SOURCE/'fmt/include'], ['-DNDEBUG'], ['probe_normal']),
        'bt': ('bt_probe.cpp', [SOURCE/'BehaviorTree.CPP/include'], [], ['probe_normal', 'probe_exception']),
        'px4': ('px4_probe.cpp', px4_includes, [], ['probe_normal']),
        'px4-ndebug': ('px4_probe.cpp', px4_includes, ['-DNDEBUG'], ['probe_normal']),
        'tinyxml2': ('tinyxml2_probe.cpp', [SOURCE/'tinyxml2'], ['-DTINYXML2_DEBUG'], ['probe_normal', 'probe_set', 'probe_return']),
        'duckdb': ('duckdb_probe.cpp', [SOURCE/'duckdb/src/include'], ['-DDEBUG'], ['probe_normal', 'probe_exception']),
        'rclcpp-fragment': ('rclcpp_fragment.cpp', [], [], ['probe_normal', 'probe_return', 'probe_exception']),
    }
    # Extract the exact function including its declaration and attach original
    # source locations. Do not provide ROS runtime stubs or an operator body.
    header = SOURCE/'rclcpp/rclcpp/include/rclcpp/detail/qos_parameters.hpp'
    lines = header.read_text().splitlines(keepends=True)
    begin = next(i for i, s in enumerate(lines) if s.startswith('check_if_stringified_policy_is_null(')) - 2
    # Its unindented closing brace is the function end.
    for i in range(begin, len(lines)):
        # Function contains a brace in a string literal; lexical brace counting
        # would be wrong. Its unindented closing brace is the function end.
        if lines[i].rstrip() == '}':
            end = i + 1
            break
    body = ''.join(lines[begin:end])
    generated = ('#include <sstream>\n#include <stdexcept>\n'
                 '#include <ikos/analyzer/intrinsic.h>\n'
                 'namespace rclcpp {\nenum class QosPolicyKind;\n'
                 'std::ostream& operator<<(std::ostream&, const QosPolicyKind&);\n'
                 'namespace detail {\n'
                 f'#line {begin+1} "{header}"\n' + body + '\n}}\n'
                 '#line 1 "rclcpp_fragment_probe.cpp"\n'
                 'extern "C" void probe_normal() {\n'
                 '  const char text[] = "x";\n'
                 '  const char* p = __ikos_nondet_int() ? text : nullptr;\n'
                 '  auto k = static_cast<rclcpp::QosPolicyKind>(__ikos_nondet_int());\n'
                 '  (void)rclcpp::detail::check_if_stringified_policy_is_null(p, k);\n'
                 '  __ikos_assert(p != nullptr);\n}\n'
                 'extern "C" void probe_return() {\n'
                 '  const char text[] = "x";\n'
                 '  const char* p = __ikos_nondet_int() ? text : nullptr;\n'
                 '  auto k = static_cast<rclcpp::QosPolicyKind>(__ikos_nondet_int());\n'
                 '  const char* result = rclcpp::detail::check_if_stringified_policy_is_null(p, k);\n'
                 '  __ikos_assert(result != nullptr);\n}\n'
                 'extern "C" void probe_exception() {\n'
                 '  const char text[] = "x";\n'
                 '  const char* p = __ikos_nondet_int() ? text : nullptr;\n'
                 '  auto k = static_cast<rclcpp::QosPolicyKind>(__ikos_nondet_int());\n'
                 '  try { (void)rclcpp::detail::check_if_stringified_policy_is_null(p, k); }\n'
                 '  catch (...) { __ikos_assert(p == nullptr); }\n}\n')
    (out/'rclcpp_fragment.cpp').write_text(generated)
    (out/'rclcpp-extraction.json').write_text(json.dumps(
        {'source': str(header), 'source_sha256': digest(header),
         'start': begin+1, 'end': end, 'body_sha256': hashlib.sha256(body.encode()).hexdigest()}, indent=2))
    results = []
    jobs = []
    for name, (src, includes, flags, entries) in cases.items():
        if args.case and name not in args.case:
            continue
        folder = out/name
        folder.mkdir(exist_ok=True)
        source_file = out/src if name == 'rclcpp-fragment' else HERE/src
        bc = folder/'probe.bc'
        command = common + ['-I'+str(p) for p in includes] + flags + [source_file, '-o', bc]
        compiled = run(command, folder/'compile.log')
        if compiled['rc']:
            print(name, 'compile failed:', (folder/'compile.log').read_text()[-2000:], flush=True)
            results.append({'case': name, 'compile': compiled})
            continue
        inputs = [bc]
        if name in ('fmt', 'fmt-ndebug'):
            inputs.append(args.cache/'fmt/core.bc')
        elif name == 'tinyxml2':
            inputs.append(args.cache/'tinyxml2/no-rtti-0.bc')
        elif name == 'duckdb':
            # Recompile the actual project source with RTTI disabled, preserving
            # all other cached flags. Unknown dependencies remain declarations.
            cmd = json.loads((args.cache/'duckdb/bitcode/1169.command.json').read_text())
            cmd[cmd.index('-o')+1] = str(folder/'assert.bc')
            cmd.append('-fno-rtti')
            extra = run(cmd, folder/'compile-assert.log')
            if extra['rc']:
                print(name, 'assert.cpp compile failed', flush=True)
                continue
            inputs.append(folder/'assert.bc')
        if len(inputs) > 1:
            bc = folder/'linked.bc'
            linked = run([LLVM/'llvm-link', *inputs, '-o', bc], folder/'link.log')
            if linked['rc']:
                print(name, 'link failed', flush=True)
                continue
        sha = digest(bc)
        for entry in entries:
            if args.entry and entry not in args.entry:
                continue
            for tool, exe in TOOLS.items():
                db = folder/(entry+'-'+tool+'.db')
                command = [exe, bc, '-a', '*', '-e', entry, '--domain=interval',
                           '--proc=inter', '--globals-init=all', '--opt=basic', '-j', '1',
                           '--color=no', '--progress=no', '--format=no', '-o', db]
                if tool == 'dev' and name != 'rclcpp-fragment':
                    command.append('--trace-ar-stmts')
                jobs.append((name, entry, tool, command, db, sha))
    def analyze(job):
        name, entry, tool, command, db, sha = job
        r = run(command, db.with_suffix('.log'), timeout=120)
        r.update(case=name, entry=entry, tool=tool, bc_sha256=sha)
        log = db.with_suffix('.log').read_text(errors='replace')
        counts = {}
        for key, word in [('safe', 'safe checks'), ('unsafe', 'definite unsafe checks'),
                          ('warning', 'warnings'), ('unreachable', 'unreachable checks')]:
            m = re.search(r'Total number of '+word+r'\s*:\s*(\d+)', log)
            if m:
                counts[key] = int(m.group(1))
        r['summary'] = counts if r['rc'] == 0 and len(counts) == 4 else None
        if r['rc'] == 0:
            export = run([REPO/'install/libexec/bin/python',
                          REPO/'benchmark/realworld_comparison/2026-11-01/export_db.py', db],
                         db.with_suffix('.export.log'))
            r['export_rc'] = export['rc']
        print(name, entry, tool, r['rc'], r['summary'], flush=True)
        return r
    with ThreadPoolExecutor(max_workers=2) as pool:
        results.extend(pool.map(analyze, jobs))
    (out/'RUNS.json').write_text(json.dumps(results, indent=2))
    if not args.case:
        # Printing all AR statements of libc++ stream classes can fail while
        # rendering recursive types. Obtain a separate frontend-only summary
        # without printing the native numerical trace. Keep this distinction.
        folder = out/'rclcpp-fragment'
        pp = folder/'probe.pp.bc'
        prep = run([REPO/'install/bin/ikos-pp', '--opt=basic',
                    '--entry-points=probe_normal', folder/'probe.bc', '-o', pp],
                   folder/'standalone-preprocess.log')
        if prep['rc'] == 0:
            run([REPO/'install/bin/ikos-import', pp, '--allow-dbg-mismatch',
                 '--format=no', '--param-symbolic'], folder/'standalone-summaries.log')
    print('DONE', out, flush=True)


if __name__ == '__main__':
    main()
