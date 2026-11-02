#!/usr/bin/env python3
"""Compile original project translation units once and compare both IKOS builds."""
from pathlib import Path
import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import shlex
import signal
import subprocess
import time

ROOT = Path(os.environ.get('IKOS_REALWORLD_RESULTS', str(Path(__file__).resolve().parent)))
SOURCES = Path('/Users/baesungmin/master/experiment/realworld')
LLVM = Path('/opt/homebrew/opt/llvm@14/bin')
TOOLS = {'original': '/Users/baesungmin/Projects/ikos/install/bin/ikos',
         'dev': '/Users/baesungmin/.local/bin/ikos-dev'}


def run(command, log, cwd=None, timeout=180):
    start = time.monotonic()
    with Path(log).open('w') as stream:
        process = subprocess.Popen(command, cwd=cwd, stdout=stream,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            code = 124
    return {'rc': code, 'seconds': round(time.monotonic()-start, 3), 'command': command}


def compile_unit(name, index):
    folder = ROOT/name
    entry = json.loads((folder/'build/compile_commands.json').read_text())[index]
    original = shlex.split(entry['command'])
    clean = []
    i = 0
    while i < len(original):
        flag = original[i]
        if flag in ('-o', '-MF', '-MT', '-MQ'):
            i += 2
            continue
        # SQLite's propagated SDK include path overrides libc++'s math.h wrapper.
        # Clang already searches this SDK by default; remove the redundant path.
        if flag == '-isystem' and original[i+1].startswith('/Library/Developer/CommandLineTools/SDKs/'):
            i += 2
            continue
        if flag.startswith('-O') or flag in ('-MD', '-MMD'):
            i += 1
            continue
        clean.append(flag)
        i += 1
    dest = folder/'bitcode'
    dest.mkdir(exist_ok=True)
    bc = dest/f'{index:04d}.bc'
    command = clean + ['-emit-llvm', '-g', '-O0', '-Xclang', '-disable-O0-optnone',
                       '-fno-discard-value-names', '-o', str(bc)]
    record = dest/f'{index:04d}.result.json'
    if record.exists():
        old = json.loads(record.read_text())
        if old['command'] == command and old['rc'] == 0 and bc.exists():
            return old
    (dest/f'{index:04d}.command.json').write_text(json.dumps(command, indent=2))
    result = run(command, dest/f'{index:04d}.log', cwd=entry['directory'])
    result.update(source=entry['file'], index=index, output=str(bc), object=entry['output'])
    if result['rc'] == 0:
        result['sha256'] = hashlib.sha256(bc.read_bytes()).hexdigest()
    record.write_text(json.dumps(result, indent=2))
    return result


def compile_project(name, indices):
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for result in pool.map(lambda index: compile_unit(name, index), indices):
            results.append(result)
            if result['rc'] or len(results) % 20 == 0:
                print(name, 'compile', len(results), '/', len(indices), 'rc', result['rc'], flush=True)
    (ROOT/name/'compile-results.json').write_text(json.dumps(results, indent=2))
    print(name, 'compiled', sum(r['rc'] == 0 for r in results), '/', len(results), flush=True)
    return results


def link(name, label, indices):
    folder = ROOT/name
    output = folder/(label+'.bc')
    command = [str(LLVM/'llvm-link'), *[str(folder/'bitcode'/f'{i:04d}.bc') for i in indices],
               '-o', str(output)]
    result = run(command, folder/(label+'.link.log'), timeout=300)
    result['indices'] = indices
    if result['rc'] == 0:
        result['sha256'] = hashlib.sha256(output.read_bytes()).hexdigest()
    (folder/(label+'.link.json')).write_text(json.dumps(result, indent=2))
    print(name, label, 'link', result['rc'], flush=True)
    return output if result['rc'] == 0 else None


def analyze(name, label, bc, entry='*', timeout=360, parallel=True):
    folder = ROOT/name/label
    folder.mkdir(exist_ok=True)
    def one(tool):
        output = folder/(tool+'.db')
        if output.exists():
            output.rename(folder/(tool+f'.previous-{time.time_ns()}.db'))
        command = [TOOLS[tool], str(bc), '-a', '*', '-e', entry, '--domain=interval',
                   '--globals-init=all', '--proc=inter', '--opt=basic', '-j', '1',
                   '--color=no', '--progress=no', '--display-times=full',
                   '--format=no', '-o', str(output)]
        (folder/(tool+'.command.json')).write_text(json.dumps(command, indent=2))
        result = run(command, folder/(tool+'.log'), timeout=timeout)
        result.update(bc_sha256=hashlib.sha256(Path(bc).read_bytes()).hexdigest(),
                      wall_timeout_seconds=timeout)
        text = (folder/(tool+'.log')).read_text(errors='replace')
        counts = {}
        for key, pattern in [('safe', 'safe checks'), ('unsafe', 'definite unsafe checks'),
                             ('warning', 'warnings'), ('unreachable', 'unreachable checks')]:
            match = re.search(r'Total number of '+pattern+r'\s*:\s*(\d+)', text)
            if match:
                counts[key] = int(match.group(1))
        result['summary'] = counts if result['rc'] == 0 and len(counts) == 4 else None
        (folder/(tool+'.result.json')).write_text(json.dumps(result, indent=2))
        print(name, label, tool, 'rc', result['rc'], 'seconds', result['seconds'],
              'summary', result['summary'], flush=True)
        if result['rc']:
            print(text[-1200:], flush=True)
        return result
    if not parallel:
        return [one(tool) for tool in TOOLS]
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        return list(pool.map(one, TOOLS))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['compile', 'analyze', 'link'])
    parser.add_argument('project')
    parser.add_argument('--indices')
    parser.add_argument('--label', default='library-all')
    parser.add_argument('--bc')
    parser.add_argument('--entry', default='*')
    parser.add_argument('--timeout', type=int, default=360)
    opt = parser.parse_args()
    indices = None
    if opt.indices:
        indices = [int(i) for i in opt.indices.split(',')]
    if opt.action == 'compile':
        if indices is None:
            indices = range(len(json.loads((ROOT/opt.project/'build/compile_commands.json').read_text())))
        compile_project(opt.project, list(indices))
    elif opt.action == 'link':
        link(opt.project, opt.label, indices)
    else:
        analyze(opt.project, opt.label, opt.bc, opt.entry, opt.timeout)


if __name__ == '__main__':
    main()
