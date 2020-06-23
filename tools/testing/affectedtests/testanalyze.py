#!/usr/bin/env python3

# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import collections
import json
import os
import subprocess
import sys
import tempfile
import unittest


def analyze(gn_path, tests_json, build_dir, changed_files):
    """
    gn_path: Local path to gn binary (e.g. /work/fuchsia/prebuilt/linux64/gn)
    tests_json: Local path to build's tests.json (e.g. out/default/tests.json)
    build_dir: Fuchsia build dir (e.g. out/default)
    changed_files: List of files to check against, relative to source root.

    Return: List of test packages that should be run.
    """
    with open(tests_json) as f:
        tests_obj = json.load(f)

    label_to_test_packages = collections.defaultdict(list)
    test_labels = []

    for t in tests_obj:
        tl = t['test']['label']
        # TODO: Understand toolchains.
        lparen = tl.rindex('(')
        toolchain = tl[lparen + 1:-1]
        tl = tl[:lparen]
        # TODO: ARM
        if toolchain == '//build/toolchain/fuchsia:x64':
            test_labels.append(tl)
            label_to_test_packages[tl].append(t['test']['package_url'])

        # TODO: https://crbug.com/gn/161 means we can't find host tests yet
        # because they're in an alternate toolchain (I think?).

    with tempfile.NamedTemporaryFile(suffix='.json', mode='wt') as tmp:
        analyze_input = {
            'files': ['//' + x for x in changed_files],
            'test_targets': test_labels,
            'additional_compile_targets': ['all'],
        }
        tmp.write(json.dumps(analyze_input))
        result = json.loads(
            subprocess.check_output(
                [gn_path, 'analyze', build_dir, tmp.name, '-']))
        if 'error' in result:
            print(result['error'], file=sys.stderr)
            return None
        if result['status'] == 'No dependency':
            return []
        else:
            ret = []
            for tl in result['test_targets']:
                ret.extend(label_to_test_packages[tl])
            return ret


def main(args):
    tests = analyze(
        args.gn_path, args.tests_json, args.build_dir,
        args.changed_files.splitlines())
    if tests is None:
        return 1
    for t in tests:
        print(t)
    return 0


def tests_for(sources):
    global BUILD_DIR_FOR_TESTS
    host_platform = (
        'linux-x64' if sys.platform.startswith('linux') else 'mac-x64')
    gn_path = os.path.join(
        BUILD_DIR_FOR_TESTS, os.pardir, os.pardir, 'prebuilt', 'third_party',
        'gn', host_platform, 'gn')
    tests_json = os.path.join(BUILD_DIR_FOR_TESTS, 'tests.json')
    return analyze(gn_path, tests_json, BUILD_DIR_FOR_TESTS, sources)


class Test(unittest.TestCase):

    def test_plain_cpp(self):
        self.assertIn(
            'fuchsia-pkg://fuchsia.com/fbl#meta/fbl-test.cmx',
            tests_for(['zircon/system/ulib/fbl/test/slab_allocator_tests.cc']))

    def test_cpp_header_change(self):
        self.assertIn(
            'fuchsia-pkg://fuchsia.com/fostr_unittests#meta/fostr_unittests.cmx',
            tests_for(['garnet/public/lib/fostr/hex_dump.h']))

    def test_multiple_packages_single_label(self):
        tests = tests_for(['sdk/lib/fidl/cpp/array_unittest.cc'])
        self.assertIn(
            'fuchsia-pkg://fuchsia.com/fidl_tests#meta/conformance_test.cmx',
            tests)
        self.assertIn(
            'fuchsia-pkg://fuchsia.com/fidl_tests#meta/fidl_cpp_unittests.cmx',
            tests)

    @unittest.skip('.rs files are not listed in sources yet.')
    def test_plain_rust(self):
        self.assertEqual(
            [
                'fuchsia-pkg://fuchsia.com/log_listener_tests#meta/log_listener_bin_test.cmx',
                'fuchsia-pkg://fuchsia.com/log_listener_tests#meta/log_listener_return_code_test.cmx',
            ], tests_for(['garnet/bin/log_listener/src/main.rs']))


def test(fake_argv, build_dir):
    global BUILD_DIR_FOR_TESTS
    BUILD_DIR_FOR_TESTS = build_dir
    unittest.main(argv=fake_argv, verbosity=2)


def parse_args_and_dispatch():
    parser = argparse.ArgumentParser(
        description='Determine affected tests, given a list of changed sources.'
    )
    parser.add_argument('--gn-path', help='path to gn binary')
    parser.add_argument(
        '--tests-json', help='path to tests.json (e.g. out/default/tests.json)')
    parser.add_argument(
        '--build-dir',
        help='path to build dir (e.g. out/default)',
        required=True)
    parser.add_argument(
        '--changed-files',
        help='sources files to be checked, separated by newline')
    parser.add_argument('action', help='either selftest or analyze')
    args = parser.parse_args()
    if args.action == 'selftest':
        sys.exit(test([sys.argv[0]], args.build_dir))
    elif args.action == 'analyze':
        sys.exit(main(args))
    else:
        sys.exit(1)


if __name__ == '__main__':
    sys.exit(parse_args_and_dispatch())
