#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import unittest

import test_env
from lib.host import Host
from test_case import TestCase


class HostTest(TestCase):

    def test_fuchsia_dir(self):
        self.assertError(
            lambda: Host(self.cli, None), 'FUCHSIA_DIR not set.',
            'Have you sourced "scripts/fx-env.sh"?')

    def test_configure(self):
        # Fails due to missing paths
        host = Host(self.cli, self.cli.getenv('FUCHSIA_DIR'))
        build_dir = 'test_configure'
        with self.assertRaises(ValueError):
            host.configure(build_dir)

        # Use the factory-created host for valid paths
        build_dir = self.host.build_dir
        host.configure(build_dir)
        clang_dir = os.path.join(
            'prebuilt', 'third_party', 'clang', self.cli.platform)

        self.assertEqual(host.build_dir, host.fxpath(build_dir))
        self.assertEqual(
            host.symbolizer_exec,
            host.fxpath(build_dir, 'host_x64', 'symbolize'))
        self.assertEqual(
            host.llvm_symbolizer,
            host.fxpath(clang_dir, 'bin', 'llvm-symbolizer'))
        self.assertEqual(
            host.build_id_dirs, [
                host.fxpath(clang_dir, 'lib', 'debug', '.build-id'),
                host.fxpath(build_dir, '.build-id'),
                host.fxpath(build_dir + '.zircon', '.build-id'),
            ])

    # Unit tests

    def test_read_fuzzers(self):
        host = self.factory.create_host()

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

        fuzzers_json = host.fxpath(host.build_dir, 'fuzzers.json')
        with self.cli.open(fuzzers_json, 'w') as opened:
            json.dump(data, opened)

        host.read_fuzzers(fuzzers_json)
        self.assertIn(('foo_fuzzers', 'foo_fuzzer'), host.fuzzers())
        self.assertIn(('zircon_fuzzers', 'zx_fuzzer.asan'), host.fuzzers())
        self.assertIn(('zircon_fuzzers', 'zx_fuzzer.ubsan'), host.fuzzers())

    def test_fuzzers(self):
        self.assertEqual(len(self.host.fuzzers('')), 6)
        self.assertEqual(len(self.host.fuzzers('/')), 6)
        self.assertEqual(len(self.host.fuzzers('fake')), 6)
        self.assertEqual(len(self.host.fuzzers('package1')), 3)
        self.assertEqual(len(self.host.fuzzers('target1')), 3)
        self.assertEqual(len(self.host.fuzzers('package2/target1')), 2)
        self.assertEqual(
            len(self.host.fuzzers('fake-package2/fake-target1')), 1)
        self.assertEqual(len(self.host.fuzzers('1/2')), 1)
        self.assertEqual(len(self.host.fuzzers('target4')), 0)
        with self.assertRaises(ValueError):
            self.host.fuzzers('a/b/c')

    def test_fxpath(self):
        self.assertEqual(
            self.host.fxpath('bar', 'baz'), self.cli.fuchsia_dir + '/bar/baz')
        self.assertEqual(
            self.host.fxpath(self.cli.fuchsia_dir, 'baz'),
            self.cli.fuchsia_dir + '/baz')

    # Other routines

    def test_fxpath(self):
        fuchsia_dir = self.cli.getenv('FUCHSIA_DIR')
        self.assertEqual(
            self.host.fxpath('bar', 'baz'),
            os.path.join(fuchsia_dir, 'bar', 'baz'))
        self.assertEqual(
            self.host.fxpath(fuchsia_dir, 'baz'),
            os.path.join(fuchsia_dir, 'baz'))

    def test_find_device(self):
        device_name = 'test_find_device'
        addrs = ['::1', '::2']

        cmd = [
            self.host.fxpath('.jiri_root', 'bin', 'fx'),
            'device-finder',
            'resolve',
            '-device-limit',
            '1',
            device_name,
        ]
        self.set_outputs(cmd, addrs[:1])
        self.assertEqual(self.host.find_device(device_name), addrs[0])

        # No results from 'fx device-finder list'
        self.assertError(
            lambda: self.host.find_device(None), 'Unable to find device.',
            'Try "fx set-device".')

        # Multiple results from `fx device-finder list`
        cmd = [
            self.host.fxpath('.jiri_root', 'bin', 'fx'), 'device-finder', 'list'
        ]
        self.set_outputs(cmd, addrs)
        self.assertError(
            lambda: self.host.find_device(None), 'Multiple devices found.',
            'Try "fx set-device".')

        # Reset output
        self.set_outputs(cmd, addrs[:1])
        self.assertEqual(self.host.find_device(None), addrs[0])

    def test_symbolize(self):
        stacktrace = [
            'a line',
            'another line',
            'yet another line',
        ]
        cmd = [
            self.host.symbolizer_exec, '-llvm-symbolizer',
            self.host.llvm_symbolizer
        ]
        for build_id_dir in self.host.build_id_dirs:
            cmd += ['-build-id-dir', build_id_dir]
        self.set_outputs(
            cmd, [
                '[000001.234567][123][456][klog] INFO: Symbolized line 1',
                '[000001.234568][123][456][klog] INFO: Symbolized line 2',
                '[000001.234569][123][456][klog] INFO: Symbolized line 3'
            ])
        symbolized = self.host.symbolize('\n'.join(stacktrace))
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
