#!/usr/bin/env python3
"""Preserve small, inspectable evidence from the diagnostic runs."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parents[2].parent/'realworld'
TERMS = {
    'fmt': ['to_unsigned', 'assert_fail'],
    'fmt-ndebug': ['to_unsigned'],
    'fmt-declaration': ['to_unsigned'],
    'px4': ['clEmm'],
    'px4-ndebug': ['clEmm'],
    'bt': ['convertNumber', 'checkUpperLimit', 'checkLowerLimit'],
    'tinyxml2': ['ReadBOM', 'SetStr'],
    'rclcpp-fragment': ['check_if_stringified_policy_is_null'],
    'duckdb': ['DuckDBAssertInternal'],
}


def summaries(path, terms):
    if not path.exists():
        return []
    result = []
    selected = False
    current = None
    component = None
    coverage = None
    with path.open(errors='replace') as stream:
        for line in stream:
            if line.startswith('Function: '):
                current = line.strip().removeprefix('Function: ')
                selected = any(term in current for term in terms)
                component = None
            if not selected:
                continue
            if line.startswith('[normal_exit]') or line.startswith('[exception_exit]'):
                component = 'normal' if line.startswith('[normal_exit]') else 'exception'
                coverage = None
                if 'none (Complete)' in line or 'no certified region (Partial)' in line:
                    result.append({'function': current, 'flow': component,
                                   'coverage': 'Complete' if 'Complete' in line else 'Partial',
                                   'condition': 'false'})
            elif line.strip().startswith('coverage: '):
                coverage = line.strip().removeprefix('coverage: ')
            elif 'C_normal (parameter' in line or 'C_exception (parameter' in line:
                condition = []
                for row in stream:
                    if not row.strip():
                        break
                    condition.append(row.strip())
                result.append({'function': current, 'flow': component,
                               'coverage': coverage, 'condition': ' '.join(condition)})
    unique = {json.dumps(r, sort_keys=True): r for r in result}
    return list(unique.values())


def main():
    roots = [Path(p) for p in sys.argv[1:]]
    runs = []
    sites = []
    for root in roots:
        for run in json.loads((root/'RUNS.json').read_text()):
            runs.append(run)
            if 'case' not in run:
                continue
            log = Path(run['log'])
            export = log.with_suffix('.export.json')
            row = {k: run[k] for k in ['case', 'entry', 'tool', 'rc', 'summary', 'bc_sha256']}
            row['log'] = str(log)
            row['assertions'] = []
            if run['rc'] == 0 and export.exists():
                data = json.loads(export.read_text())
                row['analyses'] = data['settings'].get('analyses')
                row['assertions'] = [r for r in data['reports'] if r['kind'] == 'assert']
                row['probe_warnings'] = [r for r in data['reports']
                                         if r['file'] and ('_probe.cpp' in r['file'])
                                         and r['status'] in ('warning', 'error')]
            row['native_summaries'] = summaries(log, TERMS[run['case']]) if run['tool'] == 'dev' else []
            sites.append(row)
    extracted = roots[0]/'rclcpp-fragment/standalone-summaries.txt'
    if not extracted.exists():
        extracted = roots[0]/'rclcpp-fragment/standalone-summaries.log'
    data = {'roots': list(map(str, roots)), 'runs': sites,
            'rclcpp_frontend_only_summaries': summaries(extracted, TERMS['rclcpp-fragment'])}
    (HERE/'EVIDENCE.json').write_text(json.dumps(data, indent=2))
    (HERE/'RUNS.json').write_text(json.dumps(runs, indent=2))
    for p in ['bt_probe.cpp', 'duckdb_probe.cpp', 'fmt_probe.cpp', 'px4_probe.cpp', 'tinyxml2_probe.cpp']:
        if '__ikos_assert' not in (HERE/p).read_text():
            raise RuntimeError('missing probe assertion')
    manifest = {}
    paths = {
        'BehaviorTree.CPP': ['include/behaviortree_cpp/utils/convert_impl.hpp',
                             'include/behaviortree_cpp/utils/safe_any.hpp'],
        'PX4-Autopilot': ['src/lib/matrix/matrix/Matrix.hpp', 'src/lib/matrix/matrix/Vector.hpp',
                          'platforms/common/include/px4_platform_common/defines.h'],
        'duckdb': ['src/common/assert.cpp', 'src/include/duckdb/common/assert.hpp',
                   'src/include/duckdb/common/vector.hpp', 'src/common/memory_safety.cpp',
                   'src/include/duckdb/common/types/selection_vector.hpp'],
        'fmt': ['include/fmt/core.h', 'include/fmt/format-inl.h'],
        'rclcpp': ['rclcpp/include/rclcpp/detail/qos_parameters.hpp',
                   'rclcpp_action/src/types.cpp', 'rclcpp_action/src/server.cpp'],
        'tinyxml2': ['tinyxml2.h', 'tinyxml2.cpp'],
    }
    for project, files in paths.items():
        root = SOURCE/project
        commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
        status = subprocess.check_output(['git', '-C', str(root), 'status', '--short'], text=True)
        manifest[project] = {'commit': commit, 'status': status,
                             'files': {f: hashlib.sha256((root/f).read_bytes()).hexdigest() for f in files}}
    (HERE/'SOURCES.json').write_text(json.dumps(manifest, indent=2))
    # This copied fragment carries its original #line source mapping and hash.
    (HERE/'rclcpp_fragment.cpp').write_text((roots[0]/'rclcpp_fragment.cpp').read_text())
    (HERE/'rclcpp-extraction.json').write_text((roots[0]/'rclcpp-extraction.json').read_text())
    print('Saved', len(sites), 'runs;', sum(len(s['assertions']) for s in sites), 'assertion reports')


if __name__ == '__main__':
    main()
