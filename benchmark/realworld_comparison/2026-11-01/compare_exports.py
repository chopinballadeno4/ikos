#!/usr/bin/env python3
from pathlib import Path
from collections import Counter, defaultdict
import json
import os
import subprocess

ROOT = Path(os.environ.get('IKOS_REALWORLD_RESULTS', str(Path(__file__).resolve().parent)))
PYTHON = '/Users/baesungmin/master/experiment/ikos/install/libexec/bin/python'


def key(item):
    return json.dumps({name: item[name] for name in
                       ('file', 'function', 'line', 'column', 'kind', 'operands')}, sort_keys=True)


def main():
    if not (ROOT/'fmt').is_dir():
        raise SystemExit('Set IKOS_REALWORLD_RESULTS to the directory containing the raw project results')
    pairs = []
    paths = []
    for dev in sorted(ROOT.glob('*/*/dev.result.json')):
        original = dev.with_name('original.result.json')
        if not original.exists():
            continue
        a, b = json.loads(original.read_text()), json.loads(dev.read_text())
        if not a.get('summary') or not b.get('summary'):
            continue
        assert a['bc_sha256'] == b['bc_sha256'], dev.parent
        pairs.append(dev.parent)
        for name in ('original', 'dev'):
            export = dev.parent/(name+'.export.json')
            if not export.exists() or export.stat().st_mtime < max(
                    (dev.parent/(name+'.db')).stat().st_mtime, (ROOT/'export_db.py').stat().st_mtime):
                paths.append(str(dev.parent/(name+'.db')))
    if paths:
        subprocess.run([PYTHON, str(ROOT/'export_db.py'), *paths], check=True,
                       stdout=subprocess.DEVNULL)
    comparisons = []
    for folder in pairs:
        original = json.loads((folder/'original.export.json').read_text())
        dev = json.loads((folder/'dev.export.json').read_text())
        assert set(original['settings']['analyses']) == set(dev['settings']['analyses'])
        assert len(set(dev['settings']['analyses'])) == 17
        def reports(data):
            result = defaultdict(list)
            for item in data['reports']:
                result[key(item)].append({'status': item['status'], 'info': item['info']})
            return result
        left, right = reports(original), reports(dev)
        changes = []
        for site in sorted(left.keys() | right.keys()):
            a = Counter(json.dumps(x, sort_keys=True) for x in left.get(site, []))
            b = Counter(json.dumps(x, sort_keys=True) for x in right.get(site, []))
            if a != b:
                changes.append(dict(site=json.loads(site),
                                    removed=[json.loads(x) for x in (a-b).elements()],
                                    added=[json.loads(x) for x in (b-a).elements()]))
        entry = json.loads((folder/'dev.command.json').read_text())
        comparison = dict(run=str(folder.relative_to(ROOT)),
                          entry_points=entry[entry.index('-e')+1],
                          original_summary=original['summary'],
                          dev_summary=dev['summary'], site_changes=changes,
                          raw_checker_counts_equal=original['raw_checks_by_checker'] == dev['raw_checks_by_checker'])
        (folder/'comparison.json').write_text(json.dumps(comparison, indent=2))
        comparisons.append(comparison)
    (ROOT/'completed-comparisons.json').write_text(json.dumps(comparisons, indent=2))
    print('Completed paired comparisons:', len(comparisons))
    for c in comparisons:
        if c['site_changes'] or c['original_summary'] != c['dev_summary']:
            print(c['run'], c['original_summary'], '->', c['dev_summary'],
                  'changed sites:', len(c['site_changes']))


if __name__ == '__main__':
    main()
