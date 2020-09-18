#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import unittest

import test_env
from lib.factory import Factory
from test_case import TestCaseWithFactory


class FactoryTest(TestCaseWithFactory):

    def test_create_buildenv(self):
        # FUCHSIA_DIR isn't set
        factory = Factory(host=self.host)
        self.assertError(
            lambda: factory.buildenv, 'FUCHSIA_DIR not set.',
            'Have you sourced "scripts/fx-env.sh"?')
        fuchsia_dir = self.buildenv.fuchsia_dir
        self.host.setenv('FUCHSIA_DIR', None)
        self.host.mkdir(fuchsia_dir)
        self.host.setenv('FUCHSIA_DIR', fuchsia_dir)

        # Use an empty directory to simulate an unconfigured $FUCHSIA_DIR
        build_dir = self.buildenv.build_dir
        fx_build_dir = self.buildenv.path('.fx-build-dir')
        self.assertError(
            lambda: factory.buildenv,
            'Failed to read build directory from {}.'.format(fx_build_dir),
            'Have you run "fx set ... --fuzz-with <sanitizer>"?')
        with self.host.open(fx_build_dir, 'w') as opened:
            opened.write(self.buildenv.build_dir + '\n')

        # No $FUCHSIA_DIR/out/default/fuzzer.json
        fuzzers_json = self.buildenv.path(build_dir, 'fuzzers.json')
        self.assertError(
            lambda: factory.buildenv,
            'Failed to read fuzzers from fuchsia_dir/build_dir/fuzzers.json.',
            'Have you run "fx set ... --fuzz-with <sanitizer>"?',
        )

        # Minimally valid
        with self.host.open(fuzzers_json, 'w') as opened:
            json.dump([], opened)
        buildenv = factory.buildenv
        self.assertEqual(buildenv.build_dir, buildenv.path(build_dir))
        self.assertEqual(buildenv.fuzzers(), [])

    def test_create_device(self):
        factory = Factory(host=self.host)
        # Can't create buildenv, FUCHSIA_DIR isn't set
        self.assertError(
            lambda: factory.device, 'FUCHSIA_DIR not set.',
            'Have you sourced "scripts/fx-env.sh"?')

        # Use the fake BuildEnv.
        factory._buildenv = self.buildenv

        # No $FUCHSIA_DIR/out/default.device
        build_dir = self.buildenv.build_dir
        device_addr = '::1'
        cmd = [
            self.buildenv.path('.jiri_root', 'bin', 'fx'), 'device-finder',
            'list'
        ]
        self.set_outputs(cmd, [device_addr])
        self.host.touch(self.buildenv.path(build_dir, 'ssh-keys', 'ssh_config'))
        self.assertEqual(factory.device.addr, device_addr)

        # Clear the device to force re-initialization.
        factory._device = None

        # $FUCHSIA_DIR/out/default.device present
        device_file = self.buildenv.path('{}.device'.format(build_dir))
        device_name = 'device_name'
        device_addr = '::2'
        with self.host.open(device_file, 'w') as opened:
            opened.write(device_name + '\n')
        cmd = [
            self.buildenv.path('.jiri_root', 'bin', 'fx'), 'device-finder',
            'resolve', '-device-limit', '1', device_name
        ]
        self.set_outputs(cmd, [device_addr])
        self.assertEqual(factory.device.addr, device_addr)

    def test_create_fuzzer(self):
        factory = Factory(host=self.host)

        # Use the fake BuildEnv and Device.
        factory._buildenv = self.buildenv
        factory._device = self.device

        self.host.mkdir(
            os.path.join('fuchsia_dir', 'local', 'fake-package2_fake-target1'))
        self.host.mkdir(
            os.path.join('fuchsia_dir', 'local', 'fake-package2_fake-target11'))
        self.host.mkdir('output')

        # No match
        args = self.parse_args('check', 'no/match')
        self.assertError(
            lambda: factory.create_fuzzer(args),
            'No matching fuzzers found.',
            'Try "fx fuzz list".',
        )

        # Multiple matches
        self.set_input('1')
        args = self.parse_args('check', '2/1')
        fuzzer = factory.create_fuzzer(args)
        self.assertLogged(
            'More than one match found.',
            'Please pick one from the list (or enter 0 to cancel):',
            '  1) fake-package2/fake-target1',
            '  2) fake-package2/fake-target11')
        self.assertEqual(fuzzer.package, 'fake-package2')
        self.assertEqual(fuzzer.executable, 'fake-target1')
        self.assertEqual(
            fuzzer.executable_url,
            'fuchsia-pkg://fuchsia.com/fake-package2#meta/fake-target1.cmx')

        # Exact match
        args = self.parse_args('check', '2/11')
        fuzzer = factory.create_fuzzer(args)
        self.assertEqual(fuzzer.package, 'fake-package2')
        self.assertEqual(fuzzer.executable, 'fake-target11')
        self.assertEqual(
            fuzzer.executable_url,
            'fuchsia-pkg://fuchsia.com/fake-package2#meta/fake-target11.cmx')

        # Infer from test
        args = self.parse_args('check', '1/4')
        self.assertError(
            lambda: factory.create_fuzzer(args),
            'No matching fuzzers found.',
            'Try "fx fuzz list".',
        )
        fuzzer = factory.create_fuzzer(args, include_tests=True)
        self.assertEqual(fuzzer.package, 'fake-package1')
        self.assertEqual(fuzzer.executable, 'fake-target4')

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
        fuzzer = factory.create_fuzzer(args)
        self.assertEqual(fuzzer.package, 'fake-package2')
        self.assertEqual(fuzzer.executable, 'fake-target11')
        self.assertEqual(
            fuzzer.executable_url,
            'fuchsia-pkg://fuchsia.com/fake-package2#meta/fake-target11.cmx')
        self.assertTrue(fuzzer.debug)
        self.assertTrue(fuzzer.foreground)
        self.assertTrue(fuzzer.monitor)
        self.assertEqual(fuzzer.output, 'output')
        self.assertEqual(fuzzer.libfuzzer_opts, {'output': 'foo'})
        self.assertEqual(fuzzer.subprocess_args, ['sub1', 'sub2'])


if __name__ == '__main__':
    unittest.main()
