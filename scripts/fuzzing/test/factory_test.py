#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import unittest

import test_env
from lib.factory import Factory
from test_case import TestCase


class FactoryTest(TestCase):

    def test_create_host(self):
        factory = Factory(cli=self.cli)

        # Clear $FUCHSIA_DIR
        fuchsia_dir = self.cli.getenv('FUCHSIA_DIR')
        self.cli.setenv('FUCHSIA_DIR', None)
        self.assertError(
            lambda: factory.create_host(), 'FUCHSIA_DIR not set.',
            'Have you sourced "scripts/fx-env.sh"?')

        # Use a non-existent directory to simulate an unconfigured $FUCHSIA_DIR
        self.cli.setenv('FUCHSIA_DIR', fuchsia_dir)
        build_dir = self.host.build_dir
        self.assertError(
            lambda: factory.create_host(),
            'Failed to read build directory from fuchsia_dir/.fx-build-dir.',
            'Have you run "fx set ... --fuzz-with <sanitizer>"?')

        # No $FUCHSIA_DIR/out/default/fuzzer.json
        fx_build_dir = os.path.join(self.host.fuchsia_dir, '.fx-build-dir')
        with self.cli.open(fx_build_dir, 'w') as opened:
            opened.write(build_dir + '\n')
        self.assertError(
            lambda: factory.create_host(),
            'Failed to read fuzzers from fuchsia_dir/build_dir/fuzzers.json.',
            'Have you run "fx set ... --fuzz-with <sanitizer>"?',
        )

        # Minimally valid
        fuzzers_json = os.path.join(build_dir, 'fuzzers.json')
        with self.cli.open(fuzzers_json, 'w') as opened:
            json.dump([], opened)
        host = factory.create_host()
        self.assertEqual(host.build_dir, host.fxpath(build_dir))
        self.assertEqual(host.fuzzers(), [])

    def test_create_device(self):
        factory = Factory(cli=self.cli)
        build_dir = self.host.build_dir

        # No $FUCHSIA_DIR/out/default.device
        device_addr = '::1'
        cmd = [
            self.host.fxpath('.jiri_root', 'bin', 'fx'), 'device-finder', 'list'
        ]
        self.set_outputs(cmd, [device_addr])
        self.cli.touch(self.host.fxpath(build_dir, 'ssh-keys', 'ssh_config'))
        device = factory.create_device(host=self.host)
        self.assertEqual(device.addr, device_addr)

        # $FUCHSIA_DIR/out/default.device present
        device_file = self.host.fxpath('{}.device'.format(build_dir))
        device_name = 'device_name'
        device_addr = '::2'
        with self.cli.open(device_file, 'w') as opened:
            opened.write(device_name + '\n')
        cmd = [
            self.host.fxpath('.jiri_root', 'bin', 'fx'), 'device-finder',
            'resolve', '-device-limit', '1', device_name
        ]
        self.set_outputs(cmd, [device_addr])
        device = factory.create_device(host=self.host)
        self.assertEqual(device.addr, device_addr)

    def test_create_fuzzer(self):
        factory = Factory(cli=self.cli)

        self.cli.mkdir(
            os.path.join('fuchsia_dir', 'local', 'fake-package2_fake-target1'))
        self.cli.mkdir(
            os.path.join('fuchsia_dir', 'local', 'fake-package2_fake-target11'))
        self.cli.mkdir('output')

        # No match
        args = self.parse_args('check', 'no/match')
        self.assertError(
            lambda: factory.create_fuzzer(args, device=self.device),
            'No matching fuzzers found.',
            'Try "fx fuzz list".',
        )

        # Multiple matches
        self.cli.selection = 1
        args = self.parse_args('check', '2/1')
        fuzzer = factory.create_fuzzer(args, device=self.device)
        self.assertLogged(
            'More than one match found.', 'Please pick one from the list:',
            '  1) fake-package2/fake-target1',
            '  2) fake-package2/fake-target11')
        self.assertEqual(fuzzer.package, 'fake-package2')
        self.assertEqual(fuzzer.executable, 'fake-target1')

        # Exact match
        args = self.parse_args('check', '2/11')
        fuzzer = factory.create_fuzzer(args, device=self.device)
        self.assertEqual(fuzzer.package, 'fake-package2')
        self.assertEqual(fuzzer.executable, 'fake-target11')

        # Fuzzer properties get set by args
        args = self.parse_args(
            'start',
            '--debug',
            '--foreground',
            '--monitor',
            '--output',
            'output',
            '2/11',
            '-output=foo',
            '--',
            'sub1',
            'sub2',
        )
        fuzzer = factory.create_fuzzer(args, device=self.device)
        self.assertEqual(fuzzer.package, 'fake-package2')
        self.assertEqual(fuzzer.executable, 'fake-target11')
        self.assertTrue(fuzzer.debug)
        self.assertTrue(fuzzer.foreground)
        self.assertTrue(fuzzer.monitor)
        self.assertEqual(fuzzer.output, 'output')
        self.assertEqual(fuzzer.libfuzzer_opts, {'output': 'foo'})
        self.assertEqual(fuzzer.subprocess_args, ['sub1', 'sub2'])


if __name__ == '__main__':
    unittest.main()
