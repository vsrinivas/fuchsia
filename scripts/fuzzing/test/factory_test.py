#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import tempfile
import unittest

import test_env
from lib.args import ArgParser
from lib.factory import Factory

from cli_fake import FakeCLI
from host_fake import FakeHost
from device_fake import FakeDevice


class TestFactory(unittest.TestCase):

    def test_create_host(self):
        factory = Factory(cli=FakeCLI())
        fuchsia_dir = os.getenv('FUCHSIA_DIR')

        # Simulate unsetting $FUCHSIA_DIR
        with self.assertRaises(SystemExit):
            host = factory.create_host(fuchsia_dir=None)
        self.assertEqual(
            factory.cli.log,
            ['FUCHSIA_DIR not set.', 'Have you sourced "scripts/fx-env.sh"?'])

        # Use a non-existent directory to simulate an unconfigured $FUCHSIA_DIR
        with self.assertRaises(SystemExit):
            host = factory.create_host(fuchsia_dir='fuchsia_dir')
        self.assertEqual(
            factory.cli.log, [
                'Initialization failure.',
                'Have you run `fx set ... --fuzz-with <sanitizer>`?',
            ])

        # Use the "real" $FUCHSIA_DIR
        if fuchsia_dir:
            host = factory.create_host()

    def test_create_device(self):
        # Use a FakeHost to simulate a missing default.device file.
        host = FakeHost()
        factory = Factory(cli=host.cli)

        with self.assertRaises(SystemExit):
            device = factory.create_device(host=host)
        self.assertEqual(
            factory.cli.log, [
                'Unable to find device.',
                'Try `fx set-device`.',
            ])

        # Use a temporary directory to simulate a default.device file.
        tmpdir = tempfile.mkdtemp()
        try:
            host._fuchsia_dir = tmpdir

            build_dir = 'test_create_device'
            host.add_fake_pathnames(build_dir)
            host.configure(build_dir)

            device = FakeDevice(host=host)
            device.add_fake_pathnames()

            default_device = '{}.device'.format(host.build_dir)
            device_name = 'just-four-random-words'

            with open(default_device, 'w') as opened:
                opened.write(device_name + '\n')

            device_addr = '::1'
            cmd = ' '.join(host._find_device_cmd(device_name))
            host.responses[cmd] = [device_addr]

            device = factory.create_device(host=host)
            self.assertEqual(device.addr, device_addr)
        finally:
            shutil.rmtree(tmpdir)

    def test_create_fuzzer(self):
        parser = ArgParser('test_create_fuzzer')
        factory = Factory(cli=FakeCLI())
        device = FakeDevice()

        # No match
        with self.assertRaises(SystemExit):
            fuzzer = factory.create_fuzzer(
                parser.parse([
                    'no/match',
                ]), device=device)
        self.assertEqual(
            factory.cli.log, [
                'No matching fuzzers found.',
                'Try `fx fuzz list`.',
            ])

        # Multiple matches
        factory.cli.selection = 1
        fuzzer = factory.create_fuzzer(
            parser.parse([
                '2/1',
            ]), device=device)
        self.assertEqual(
            factory.cli.log, [
                'More than one match found.',
                'Please pick one from the list:',
            ])
        self.assertEqual(fuzzer.package, 'fake-package2')
        self.assertEqual(fuzzer.executable, 'fake-target1')

        # Exact match
        fuzzer = factory.create_fuzzer(
            parser.parse([
                '2/11',
            ]), device=device)
        self.assertEqual(fuzzer.package, 'fake-package2')
        self.assertEqual(fuzzer.executable, 'fake-target11')

        # Fuzzer properties get set by args
        device.host.pathnames.append('output')
        fuzzer = factory.create_fuzzer(
            parser.parse(
                [
                    '--debug',
                    '--foreground',
                    '--monitor',
                    '--output',
                    'output',
                    '2/11',
                    '-output=foo',
                    'input1',
                    'input2',
                    '--',
                    'sub1',
                    'sub2',
                ]),
            device=device)
        self.assertEqual(fuzzer.package, 'fake-package2')
        self.assertEqual(fuzzer.executable, 'fake-target11')
        self.assertTrue(fuzzer.debug)
        self.assertTrue(fuzzer.foreground)
        self.assertTrue(fuzzer.monitor)
        self.assertEqual(fuzzer.output, 'output')
        self.assertEqual(fuzzer.libfuzzer_opts, {'output': 'foo'})
        self.assertEqual(fuzzer.libfuzzer_inputs, ['input1', 'input2'])
        self.assertEqual(fuzzer.subprocess_args, ['sub1', 'sub2'])


if __name__ == '__main__':
    unittest.main()
