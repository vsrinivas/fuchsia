#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import unittest

import test_env
from lib.buildenv import BuildEnv
from test_case import TestCaseWithFactory


class BuildEnvTest(TestCaseWithFactory):

    def test_fuchsia_dir(self):
        self.assertError(
            lambda: BuildEnv(self.cli, None), 'FUCHSIA_DIR not set.',
            'Have you sourced "scripts/fx-env.sh"?')

    def test_configure(self):
        # Fails due to missing paths
        fuchsia_dir = 'test_configure'
        buildenv = BuildEnv(self.cli, fuchsia_dir)
        build_dir = os.path.join(fuchsia_dir, 'build_dir')
        self.cli.mkdir(build_dir)

        # No $FUCHSIA_DIR/out/default/host_x64/symbolize
        symbolizer_exec = os.path.join(build_dir, 'host_x64', 'symbolize')
        self.assertError(
            lambda: buildenv.configure(build_dir),
            'Invalid symbolizer executable: {}'.format(symbolizer_exec))
        self.cli.touch(symbolizer_exec)

        # No $FUCHSIA_DIR/out/default/host_x64/symbolize
        clang_dir = os.path.join(
            fuchsia_dir, 'prebuilt', 'third_party', 'clang', self.cli.platform)
        llvm_symbolizer = os.path.join(clang_dir, 'bin', 'llvm-symbolizer')
        self.assertError(
            lambda: buildenv.configure(build_dir),
            'Invalid LLVM symbolizer: {}'.format(llvm_symbolizer))
        self.cli.touch(llvm_symbolizer)

        # No $FUCHSIA_DIR/.../.build-id
        build_id_dirs = [
            os.path.join(clang_dir, 'lib', 'debug', '.build-id'),
            os.path.join(build_dir, '.build-id'),
            os.path.join(build_dir + '.zircon', '.build-id'),
        ]
        for build_id_dir in build_id_dirs:
            self.assertError(
                lambda: buildenv.configure(build_dir),
                'Invalid build ID directory: {}'.format(build_id_dir),
            )
            self.cli.mkdir(build_id_dir)

        buildenv.configure(build_dir)
        clang_dir = os.path.join(
            'prebuilt', 'third_party', 'clang', self.cli.platform)

        self.assertEqual(buildenv.build_dir, buildenv.path(build_dir))
        self.assertEqual(
            buildenv.symbolizer_exec,
            buildenv.path(build_dir, 'host_x64', 'symbolize'))
        self.assertEqual(
            buildenv.llvm_symbolizer,
            buildenv.path(clang_dir, 'bin', 'llvm-symbolizer'))
        self.assertEqual(
            buildenv.build_id_dirs, [
                buildenv.path(clang_dir, 'lib', 'debug', '.build-id'),
                buildenv.path(build_dir, '.build-id'),
                buildenv.path(build_dir + '.zircon', '.build-id'),
            ])

    # Unit tests

    def test_read_fuzzers(self):
        buildenv = self.factory.create_buildenv()

        # Construct and parse both fuchsia and zircon style fuzzer metadata.
        data = [
            {
                'fuzz_host': False,
                'fuzzers': ['foo_fuzzer'],
                'fuzzers_package': 'foo_fuzzers'
            },
            {
                'fuzz_host': False,
                'fuzzers': ['zx_fuzzer.asan', 'zx_fuzzer.ubsan'],
                'fuzzers_package': 'zircon_fuzzers'
            },
        ]

        fuzzers_json = buildenv.path(buildenv.build_dir, 'fuzzers.json')
        with self.cli.open(fuzzers_json, 'w') as opened:
            json.dump(data, opened)

        buildenv.read_fuzzers(fuzzers_json)
        self.assertIn(('foo_fuzzers', 'foo_fuzzer'), buildenv.fuzzers())
        self.assertIn(('zircon_fuzzers', 'zx_fuzzer.asan'), buildenv.fuzzers())
        self.assertIn(('zircon_fuzzers', 'zx_fuzzer.ubsan'), buildenv.fuzzers())

    def test_fuzzers(self):
        self.assertEqual(len(self.buildenv.fuzzers('')), 6)
        self.assertEqual(len(self.buildenv.fuzzers('/')), 6)
        self.assertEqual(len(self.buildenv.fuzzers('fake')), 6)
        self.assertEqual(len(self.buildenv.fuzzers('package1')), 3)
        self.assertEqual(len(self.buildenv.fuzzers('target1')), 3)
        self.assertEqual(len(self.buildenv.fuzzers('package2/target1')), 2)
        self.assertEqual(
            len(self.buildenv.fuzzers('fake-package2/fake-target1')), 1)
        self.assertEqual(len(self.buildenv.fuzzers('1/2')), 1)
        self.assertEqual(len(self.buildenv.fuzzers('target4')), 0)
        with self.assertRaises(ValueError):
            self.buildenv.fuzzers('a/b/c')

    def test_path(self):
        fuchsia_dir = self.cli.getenv('FUCHSIA_DIR')
        self.assertEqual(
            self.buildenv.path('bar', 'baz'),
            os.path.join(fuchsia_dir, 'bar', 'baz'))
        self.assertEqual(
            self.buildenv.path(fuchsia_dir, 'baz'),
            os.path.join(fuchsia_dir, 'baz'))

    def test_find_device(self):
        device_name = 'test_find_device'
        addrs = ['::1', '::2']

        cmd = [
            self.buildenv.path('.jiri_root', 'bin', 'fx'),
            'device-finder',
            'resolve',
            '-device-limit',
            '1',
            device_name,
        ]
        self.set_outputs(cmd, addrs[:1])
        self.assertEqual(self.buildenv.find_device(device_name), addrs[0])

        # No results from 'fx device-finder list'
        self.assertError(
            lambda: self.buildenv.find_device(None), 'Unable to find device.',
            'Try "fx set-device".')

        # Multiple results from `fx device-finder list`
        cmd = [
            self.buildenv.path('.jiri_root', 'bin', 'fx'), 'device-finder',
            'list'
        ]
        self.set_outputs(cmd, addrs)
        self.assertError(
            lambda: self.buildenv.find_device(None), 'Multiple devices found.',
            'Try "fx set-device".')

        # Reset output
        self.set_outputs(cmd, addrs[:1])
        self.assertEqual(self.buildenv.find_device(None), addrs[0])

    def test_symbolize(self):
        stacktrace = [
            'a line',
            'another line',
            'yet another line',
        ]
        cmd = [
            self.buildenv.symbolizer_exec, '-llvm-symbolizer',
            self.buildenv.llvm_symbolizer
        ]
        for build_id_dir in self.buildenv.build_id_dirs:
            cmd += ['-build-id-dir', build_id_dir]
        self.set_outputs(
            cmd, [
                '[000001.234567][123][456][klog] INFO: Symbolized line 1',
                '[000001.234568][123][456][klog] INFO: Symbolized line 2',
                '[000001.234569][123][456][klog] INFO: Symbolized line 3'
            ])
        symbolized = self.buildenv.symbolize('\n'.join(stacktrace))
        self.assertRan(*cmd)
        self.assertInputs(cmd, stacktrace)
        self.assertEqual(
            symbolized.strip().split('\n'), [
                'Symbolized line 1',
                'Symbolized line 2',
                'Symbolized line 3',
            ])


if __name__ == '__main__':
    unittest.main()
