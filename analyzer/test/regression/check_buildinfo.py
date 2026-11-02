#!/usr/bin/env python3
"""Check build snapshots, changed sources and stale installed executables."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
from unittest import mock


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--module', type=Path, required=True)
    parser.add_argument('--analyzer', type=Path, required=True)
    options = parser.parse_args()
    spec = importlib.util.spec_from_file_location('audit_buildinfo', options.module)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    actual = module.analyzer_info(options.analyzer)
    assert actual['dev_version'] and len(actual['source_sha256']) == 64
    banner = subprocess.check_output([str(options.analyzer), '--version'], text=True)
    assert actual['build_id'] in banner and actual['dev_version'] in banner, banner
    assert 'Source check: MATCH' in module.version_lines(options.analyzer)
    assert 'Install check: MATCH' in module.version_lines(options.analyzer)

    with tempfile.TemporaryDirectory(prefix='ikos-buildinfo-test-') as folder:
        root = Path(folder)
        (root / 'DEV_VERSION').write_text('0.1.0\n')
        cpp_source = root / 'analyzer/src/test.cpp'
        cpp_source.parent.mkdir(parents=True)
        cpp_source.write_text('int value = 1;\n')
        binary, installed = root / 'build/analyzer', root / 'install/analyzer'
        for path in (binary, installed):
            path.parent.mkdir(parents=True)
            path.write_bytes(b'first-build')
        output = root / 'build/build-info.cpp'
        opts = argparse.Namespace(source_root=root, build_binary=binary,
                                  upstream='3.5', build_mode='Debug',
                                  compiler='test compiler', build_flags='',
                                  llvm_version='14', features='', output=output)
        module.generate(opts)
        info = json.loads(output.with_suffix('.json').read_text())
        initial = output.read_bytes()
        initial_mtime = output.stat().st_mtime_ns
        module.generate(opts)
        assert output.read_bytes() == initial
        assert output.stat().st_mtime_ns == initial_mtime, 'no-op generation rewrote C++ output'
        # Docs/benchmarks are not analyzer source changes.
        (root / 'README.md').write_text('documentation edit')
        assert module.source_digest(root) == info['source_sha256']
        with mock.patch.object(module, 'analyzer_info', return_value=info):
            assert 'Source check: MATCH' in module.version_lines(installed)
            assert 'Install check: MATCH' in module.version_lines(installed)
            cpp_source.write_text('int value = 2;\n')
            assert any('Source check: STALE' in s for s in module.version_lines(installed))
            # A changed checkout must not change the reported built snapshot.
            assert info['build_id'] in '\n'.join(module.version_lines(installed))
            binary.write_bytes(b'new-build-not-installed')
            assert any('Install check: STALE' in s for s in module.version_lines(installed))
        module.generate(opts)
        updated = json.loads(output.with_suffix('.json').read_text())
        assert updated['build_id'] != info['build_id']
        assert updated['source_sha256'] != info['source_sha256']
    print('Build info: native identity, no-op generation, source edits and stale installs verified')


if __name__ == '__main__':
    main()
