#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Tests for rustc_remote_wrapper."""

import rustc_remote_wrapper
import unittest

from unittest import mock


class MainArgParseTests(unittest.TestCase):

    def testDefaults(self):
        args = rustc_remote_wrapper.parse_main_args([])
        self.assertFalse(args.dry_run)
        self.assertFalse(args.local)
        self.assertFalse(args.verbose)
        self.assertFalse(args.fsatrace)
        self.assertEqual(args.command, [])

    def testDefaultOpposites(self):
        args = rustc_remote_wrapper.parse_main_args(
            [
                '--dry-run', '--local', '--verbose', '--fsatrace', '--',
                'rustc', 'src/lib.rs'
            ])
        self.assertTrue(args.dry_run)
        self.assertTrue(args.local)
        self.assertTrue(args.verbose)
        self.assertTrue(args.fsatrace)
        self.assertEqual(args.command, ['rustc', 'src/lib.rs'])


class CommandArgParseTests(unittest.TestCase):

    def testDefaults(self):
        args = rustc_remote_wrapper.parse_compile_command([])
        self.assertFalse(args.remote_disable)
        self.assertEqual(args.remote_inputs, [])
        self.assertEqual(args.remote_outputs, [])
        self.assertEqual(args.remote_flag, [])

    def testRemoteDisable(self):
        args = rustc_remote_wrapper.parse_compile_command(['--remote-disable'])
        self.assertTrue(args.remote_disable)

    def testRemoteInputs(self):
        args = rustc_remote_wrapper.parse_compile_command(
            ['--remote-inputs', 'aaa,bbb'])
        self.assertEqual(args.remote_inputs, 'aaa,bbb')

        args = rustc_remote_wrapper.parse_compile_command(
            ['--remote-inputs=aaa,bbb'])
        self.assertEqual(args.remote_inputs, 'aaa,bbb')

        args = rustc_remote_wrapper.parse_compile_command(
            ['--remote-inputs', 'aaa', '--remote-inputs', 'bbb'])
        self.assertEqual(args.remote_inputs, 'bbb')

    def testRemoteOutputs(self):
        args = rustc_remote_wrapper.parse_compile_command(
            ['--remote-outputs', 'xxx,yyy'])
        self.assertEqual(args.remote_outputs, 'xxx,yyy')

        args = rustc_remote_wrapper.parse_compile_command(
            ['--remote-outputs=xxx,yyy'])
        self.assertEqual(args.remote_outputs, 'xxx,yyy')

        args = rustc_remote_wrapper.parse_compile_command(
            ['--remote-outputs', 'xxx', '--remote-outputs', 'yyy'])
        self.assertEqual(args.remote_outputs, 'yyy')

    def testRemoteFlag(self):
        # argparse does not handle this:
        # args = rustc_remote_wrapper.parse_compile_command(['--remote-flag', '--some-rewrapper-flag=foobar'])
        # self.assertEqual(args.remote_flag, ['--some-rewrapper-flag=foobar'])

        args = rustc_remote_wrapper.parse_compile_command(
            ['--remote-flag=--some-rewrapper-flag=foobar'])
        self.assertEqual(args.remote_flag, ['--some-rewrapper-flag=foobar'])

        args = rustc_remote_wrapper.parse_compile_command(
            ['--remote-flag=--some-rewrapper-flag=foobar', '--remote-flag=baz'])
        self.assertEqual(
            args.remote_flag, ['--some-rewrapper-flag=foobar', 'baz'])


class RemoteCommandPseudoFlagsTests(unittest.TestCase):

    def testCommandFiltering(self):
        test_cases = [
            # (input, expected)
            (
                [],
                [],
            ),
            (
                ['foo', 'bar'],
                ['foo', 'bar'],
            ),
            (
                ['--remote-disable', 'foo', 'bar'],
                ['foo', 'bar'],
            ),
            (
                ['foo', '--remote-inputs=bar'],
                ['foo'],
            ),
            (
                ['--remote-outputs=foo', 'bar'],
                ['bar'],
            ),
            (
                ['foo', '--remote-flag=--bar', 'baz'],
                ['foo', 'baz'],
            ),
        ]
        for test_case in test_cases:
            self.assertEqual(
                list(
                    rustc_remote_wrapper.remove_command_pseudo_flags(
                        test_case[0])), test_case[1])


class ApplyRemoteFlagsFromPseudoFlagsTests(unittest.TestCase):

    def testDefault(self):
        test_cases = [
            # (full command, expected value of .local)
            ([], False),
            (['--'], False),
            (['--local', '--'], True),
            (['--', '--remote-disable'], True),
        ]
        for i, test_case in enumerate(test_cases):
            main_config = rustc_remote_wrapper.parse_main_args(test_case[0])
            command_params = rustc_remote_wrapper.parse_compile_command(
                main_config.command)
            rustc_remote_wrapper.apply_remote_flags_from_pseudo_flags(
                main_config, command_params)
            self.assertEqual(main_config.local, test_case[1])


if __name__ == '__main__':
    unittest.main()
