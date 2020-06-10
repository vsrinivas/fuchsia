#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import sys
import tempfile
import unittest

import test_env
import lib.command as command

from device_test import DeviceTestCase
from device_fake import FakeDevice
from fuzzer_fake import FakeFuzzer
from factory_fake import FakeFactory


class TestCommand(DeviceTestCase):

    # Unit tests

    def test_list_fuzzers(self):
        factory = FakeFactory()
        parser = factory.create_parser()

        # No match
        args = parser.parse_args(['list', 'no/match'])
        command.list_fuzzers(args, factory)
        self.assertEqual(factory.cli.log, ['No matching fuzzers.'])

        # Multiple matches
        args = parser.parse_args(['list', 'fake-package1'])
        command.list_fuzzers(args, factory)
        self.assertEqual(
            factory.cli.log, [
                'Found 3 matching fuzzers:',
                '  fake-package1/fake-target1',
                '  fake-package1/fake-target2',
                '  fake-package1/fake-target3',
            ])

        # Exact match
        args = parser.parse_args(['list', 'fake-package1/fake-target1'])
        command.list_fuzzers(args, factory)
        self.assertEqual(
            factory.cli.log, [
                'Found 1 matching fuzzers:',
                '  fake-package1/fake-target1',
            ])

    def test_start_fuzzer(self):
        factory = FakeFactory()
        parser = factory.create_parser()

        output = 'output'
        factory.host.mkdir(output)

        # In the foreground
        args = parser.parse_args(
            [
                'start',
                '--foreground',
                '--output',
                output,
                'fake-package1/fake-target1',
            ])
        command.start_fuzzer(args, factory)
        self.assertEqual(
            factory.cli.log, [
                'Starting fake-package1/fake-target1.',
                'Outputs will be written to: output',
            ])

        # In the background
        args = parser.parse_args(
            [
                'start',
                '--output',
                output,
                'fake-package1/fake-target1',
            ])
        command.start_fuzzer(args, factory)
        self.assertEqual(
            factory.cli.log, [
                'Starting fake-package1/fake-target1.',
                'Outputs will be written to: output',
                'Check status with "fx fuzz check fake-package1/fake-target1".',
                'Stop manually with "fx fuzz stop fake-package1/fake-target1".',
            ])
        cmd = [
            'python',
            sys.argv[0],
            'start',
            '--monitor',
            '--output',
            output,
            'fake-package1/fake-target1',
        ]
        self.assertRan(factory.host, *cmd)

        args = parser.parse_args(
            [
                'start',
                '--monitor',
                '--output',
                output,
                'fake-package1/fake-target1',
            ])
        command.start_fuzzer(args, factory)
        self.assertEqual(
            factory.cli.log, [
                'fake-package1/fake-target1 has stopped.',
                'Output written to: output.',
            ])

    def test_check_fuzzer(self):
        factory = FakeFactory()
        parser = factory.create_parser()

        # No name, none running
        args = parser.parse_args(['check'])
        command.check_fuzzer(args, factory)
        self.assertEqual(
            factory.cli.log, [
                'No fuzzers are running.',
                'Include \'name\' to check specific fuzzers.',
            ])

        # No name, some running
        factory.device.add_fake_pid('fake-package1', 'fake-target2')
        args = parser.parse_args(['check'])
        command.check_fuzzer(args, factory)
        self.assertEqual(
            factory.cli.log, [
                'fake-package1/fake-target2: RUNNING',
                '    Output path:  fuchsia_dir/local/fake-package1_fake-target2',
                '    Corpus size:  0 inputs / 0 bytes',
                '    Artifacts:    0',
            ])

        # Name provided, running
        args = parser.parse_args(['check', 'fake-package1/fake-target2'])
        command.check_fuzzer(args, factory)
        self.assertEqual(
            factory.cli.log, [
                'fake-package1/fake-target2: RUNNING',
                '    Output path:  fuchsia_dir/local/fake-package1_fake-target2',
                '    Corpus size:  0 inputs / 0 bytes',
                '    Artifacts:    0',
            ])

        # Name provided, not running
        factory.device.clear_fake_pids()
        args = parser.parse_args(['check', 'fake-package1/fake-target2'])
        command.check_fuzzer(args, factory)
        self.assertEqual(
            factory.cli.log, [
                'fake-package1/fake-target2: STOPPED',
                '    Output path:  fuchsia_dir/local/fake-package1_fake-target2',
                '    Corpus size:  0 inputs / 0 bytes',
                '    Artifacts:    0',
            ])

    def test_stop_fuzzer(self):
        factory = FakeFactory()
        parser = factory.create_parser()

        # Not running
        args = parser.parse_args(['stop', 'fake-package1/fake-target3'])
        command.stop_fuzzer(args, factory)
        self.assertEqual(
            factory.cli.log, ['fake-package1/fake-target3 is already stopped.'])

        # Running
        factory.device.add_fake_pid('fake-package1', 'fake-target3')
        args = parser.parse_args(['stop', 'fake-package1/fake-target3'])
        command.stop_fuzzer(args, factory)
        self.assertEqual(
            factory.cli.log, ['Stopping fake-package1/fake-target3.'])

    def test_repro_units(self):
        factory = FakeFactory()
        parser = factory.create_parser()

        unit = 'crash-deadbeef'
        factory.host.pathnames.append(unit)
        args = parser.parse_args(['repro', 'fake-package1/fake-target3', unit])
        command.repro_units(args, factory)


if __name__ == '__main__':
    unittest.main()
