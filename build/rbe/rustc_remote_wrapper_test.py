#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Tests for rustc_remote_wrapper."""

import rustc_remote_wrapper

import argparse
import parameterized
import unittest

from parameterized import parameterized
from unittest import mock


class MainArgParseTests(unittest.TestCase):

    def testDefaults(self):
        args, forward = rustc_remote_wrapper.parse_main_args([])
        self.assertIsNone(args.help)
        self.assertFalse(args.dry_run)
        self.assertFalse(args.local)
        self.assertFalse(args.verbose)
        self.assertFalse(args.fsatrace)
        self.assertEqual(args.command, [])
        self.assertEqual(forward, [])

    @parameterized.expand(
        [
            (['-h'],),
            (['--help'],),
            (['--help', '--local'],),
            (['--local', '--help'],),
            (['--local', '--help', '--', 'echo', 'ekko'],),
        ])
    def testHelp(self, flags):
        args, forward = rustc_remote_wrapper.parse_main_args(flags)
        self.assertIsNotNone(args.help)
        # don't care about other fields

    def testDefaultOpposites(self):
        args, forward = rustc_remote_wrapper.parse_main_args(
            [
                '--dry-run', '--local', '--verbose', '--fsatrace', '--',
                'rustc', 'src/lib.rs'
            ])
        self.assertIsNone(args.help)
        self.assertTrue(args.dry_run)
        self.assertTrue(args.local)
        self.assertTrue(args.verbose)
        self.assertTrue(args.fsatrace)
        self.assertEqual(args.command, ['rustc', 'src/lib.rs'])
        self.assertEqual(forward, [])

    def testForwardRewrapperArgs(self):
        args, forward = rustc_remote_wrapper.parse_main_args(
            [
                '--local', '--forward-me', 'arg1', '--forward-me=too',
                '--verbose', '--', 'rustc', 'src/lib.rs'
            ])
        self.assertIsNone(args.help)
        self.assertTrue(args.local)
        self.assertTrue(args.verbose)
        self.assertEqual(forward, ['--forward-me', 'arg1', '--forward-me=too'])
        self.assertEqual(args.command, ['rustc', 'src/lib.rs'])


class FilterCompileCommandTests(unittest.TestCase):

    def testDefaults(self):
        remote_params, filtered_command = rustc_remote_wrapper.filter_compile_command(
            [])
        self.assertFalse(remote_params.remote_disable)
        self.assertEqual(remote_params.remote_inputs, [])
        self.assertEqual(remote_params.remote_outputs, [])
        self.assertEqual(remote_params.remote_flags, [])
        self.assertEqual(filtered_command, [])

    def testNormalCommand(self):
        remote_params, filtered_command = rustc_remote_wrapper.filter_compile_command(
            ['echo', 'hello'])
        self.assertFalse(remote_params.remote_disable)
        self.assertEqual(remote_params.remote_inputs, [])
        self.assertEqual(remote_params.remote_outputs, [])
        self.assertEqual(remote_params.remote_flags, [])
        self.assertEqual(filtered_command, ['echo', 'hello'])

    def testRemoteDisable(self):
        remote_params, filtered_command = rustc_remote_wrapper.filter_compile_command(
            ['--remote-disable'])
        self.assertTrue(remote_params.remote_disable)
        self.assertEqual(filtered_command, [])

    def testRemoteInputs(self):
        remote_params, filtered_command = rustc_remote_wrapper.filter_compile_command(
            ['--remote-inputs', 'aaa,bbb'])
        self.assertEqual(remote_params.remote_inputs, 'aaa,bbb')
        self.assertEqual(filtered_command, [])

        remote_params, filtered_command = rustc_remote_wrapper.filter_compile_command(
            ['--remote-inputs=aaa,bbb'])
        self.assertEqual(remote_params.remote_inputs, 'aaa,bbb')
        self.assertEqual(filtered_command, [])

        remote_params, filtered_command = rustc_remote_wrapper.filter_compile_command(
            ['--remote-inputs', 'aaa', '--remote-inputs', 'bbb'])
        self.assertEqual(remote_params.remote_inputs, 'bbb')
        self.assertEqual(filtered_command, [])

    def testRemoteOutputs(self):
        remote_params, filtered_command = rustc_remote_wrapper.filter_compile_command(
            ['--remote-outputs', 'xxx,yyy'])
        self.assertEqual(remote_params.remote_outputs, 'xxx,yyy')
        self.assertEqual(filtered_command, [])

        remote_params, filtered_command = rustc_remote_wrapper.filter_compile_command(
            ['--remote-outputs=xxx,yyy'])
        self.assertEqual(remote_params.remote_outputs, 'xxx,yyy')
        self.assertEqual(filtered_command, [])

        remote_params, filtered_command = rustc_remote_wrapper.filter_compile_command(
            ['--remote-outputs', 'xxx', '--remote-outputs', 'yyy'])
        self.assertEqual(remote_params.remote_outputs, 'yyy')
        self.assertEqual(filtered_command, [])

    def testRemoteFlag(self):
        # argparse does not handle this:
        # remote_params = rustc_remote_wrapper.filter_compile_command(['--remote-flag', '--some-rewrapper-flag=foobar'])
        # self.assertEqual(remote_params.remote_flags, ['--some-rewrapper-flag=foobar'])

        remote_params, filtered_command = rustc_remote_wrapper.filter_compile_command(
            ['--remote-flag=--some-rewrapper-flag=foobar'])
        self.assertEqual(
            remote_params.remote_flags, ['--some-rewrapper-flag=foobar'])
        self.assertEqual(filtered_command, [])

        remote_params, filtered_command = rustc_remote_wrapper.filter_compile_command(
            ['--remote-flag=--some-rewrapper-flag=foobar', '--remote-flag=baz'])
        self.assertEqual(
            remote_params.remote_flags, ['--some-rewrapper-flag=foobar', 'baz'])
        self.assertEqual(filtered_command, [])

    @parameterized.expand(
        [
            # (input command, expected remote params, expected filtered command)
            (
                [],
                argparse.Namespace(
                    remote_disable=False,
                    remote_inputs=[],
                    remote_outputs=[],
                    remote_flags=[]),
                [],
            ),
            (
                ['make', 'it', 'so'],
                argparse.Namespace(
                    remote_disable=False,
                    remote_inputs=[],
                    remote_outputs=[],
                    remote_flags=[]),
                ['make', 'it', 'so'],
            ),
            (
                ['make', 'it', '--remote-disable', 'so'],
                argparse.Namespace(
                    remote_disable=True,
                    remote_inputs=[],
                    remote_outputs=[],
                    remote_flags=[]),
                ['make', 'it', 'so'],
            ),
            (
                ['make', '--remote-flag=--rewrapper-flag', 'it', 'so'],
                argparse.Namespace(
                    remote_disable=False,
                    remote_inputs=[],
                    remote_outputs=[],
                    remote_flags=['--rewrapper-flag']),
                ['make', 'it', 'so'],
            ),
            (
                ['make', '--remote-flag', '--rewrapper-flag', 'it', 'so'],
                argparse.Namespace(
                    remote_disable=False,
                    remote_inputs=[],
                    remote_outputs=[],
                    remote_flags=['--rewrapper-flag']),
                ['make', 'it', 'so'],
            ),
        ])
    def testGeneralCases(
            self, input_command, expected_remote_params,
            expected_filtered_command):
        remote_params, filtered_command = rustc_remote_wrapper.filter_compile_command(
            input_command)
        self.assertEqual(remote_params, expected_remote_params)
        self.assertEqual(filtered_command, expected_filtered_command)


class ApplyRemoteFlagsFromPseudoFlagsTests(unittest.TestCase):

    @parameterized.expand(
        [
            # (full command, expected value of .local)
            ([], False),
            (['--'], False),
            (['--local', '--'], True),
            (['--', '--remote-disable'], True),
        ])
    def testDefault(self, input_command, expected_local):
        main_config, rewrapper_opts = rustc_remote_wrapper.parse_main_args(
            input_command)
        remote_params, filtered_command = rustc_remote_wrapper.filter_compile_command(
            main_config.command)
        rustc_remote_wrapper.apply_remote_flags_from_pseudo_flags(
            main_config, remote_params)
        self.assertEqual(main_config.local, expected_local)


_ENV = rustc_remote_wrapper._ENV

_FAKE_GLOBALS = rustc_remote_wrapper._dependent_globals(
    '../../script.py', 'out/not-default')


class ParseRustCompileCommandTests(unittest.TestCase):

    def testEmpty(self):
        # Make sure degenerate case doesn't crash.
        self.assertEqual(
            rustc_remote_wrapper.parse_rust_compile_command([], _FAKE_GLOBALS),
            argparse.Namespace(
                depfile=None,
                dep_only_command=[_ENV],
            ))

    @parameterized.expand(
        [
            (
                ['rustc', '--blah', '--emit=dep-info=foo/bar.d', '--bar=baz'],
                'foo/bar.d',
                [
                    _ENV, 'rustc', '--blah', '-Zbinary-dep-depinfo',
                    '--emit=dep-info=foo/bar.d.nolink', '--bar=baz'
                ],
            ),
            (  # with extra ./
                ['rustc', '--blah', '--emit=dep-info=./foo/bar.d', '--bar=baz'],
                'foo/bar.d',
                [
                    _ENV, 'rustc', '--blah', '-Zbinary-dep-depinfo',
                    '--emit=dep-info=foo/bar.d.nolink', '--bar=baz'
                ],
            ),
            (  # with link
                ['rustc', '--blah', '--emit=dep-info=foo/bar.d,link', '--bar=baz'],
                'foo/bar.d',
                [
                    _ENV, 'rustc', '--blah', '-Zbinary-dep-depinfo',
                    '--emit=dep-info=foo/bar.d.nolink', '--bar=baz'
                ],
            ),
            (  # with link
                ['rustc', '--blah', '--emit=link,dep-info=foo/bar.d', '--bar=baz'],
                'foo/bar.d',
                [
                    _ENV, 'rustc', '--blah', '-Zbinary-dep-depinfo',
                    '--emit=dep-info=foo/bar.d.nolink', '--bar=baz'
                ],
            ),
        ])
    def testRustCommandsWithDepInfo(
            self, command, expected_depfile, expected_dep_command):
        params = rustc_remote_wrapper.parse_rust_compile_command(
            command, _FAKE_GLOBALS)
        self.assertEqual(params.depfile, expected_depfile)
        self.assertEqual(params.dep_only_command, expected_dep_command)


if __name__ == '__main__':
    unittest.main()
