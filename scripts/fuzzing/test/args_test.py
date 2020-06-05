#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

import test_env
from lib.args import ArgParser
from collections import namedtuple


class TestArgParser(unittest.TestCase):

    # Helper method to check all possible arguments
    def assertArgsEqual(
            self,
            args,
            debug=False,
            foreground=False,
            label=None,
            monitor=False,
            name=None,
            no_cipd=False,
            output=None,
            staging=None):
        self.assertEqual(args.debug, debug)
        self.assertEqual(args.foreground, foreground)
        if hasattr(args, 'label'):
            self.assertEqual(args.label, label)
        self.assertEqual(args.monitor, monitor)
        self.assertEqual(args.name, name)
        self.assertEqual(args.no_cipd, no_cipd)
        self.assertEqual(args.output, output)
        self.assertEqual(args.staging, staging)

    def test_parse_args_name(self):
        parser = ArgParser('test_parse_args_name')

        # By default, 'name' is required
        parser.require_name(True)
        with self.assertRaises(SystemExit):
            parser.parse_args([])

        args = parser.parse_args(['name'])
        self.assertArgsEqual(args, name='name')

        # 'name' can be made optional.
        parser.require_name(False)
        args = parser.parse_args([])
        self.assertArgsEqual(args)

        args = parser.parse_args(['name'])
        self.assertArgsEqual(args, name='name')

    def test_parse_args_label(self):
        parser = ArgParser('test_parse_args_label')

        # Error if label present when not allowed.
        with self.assertRaises(SystemExit):
            parser.parse_args(['name', 'label'])

        # label not allowed if name is optional.
        parser.require_name(False)
        with self.assertRaises(AssertionError):
            parser.allow_label(True)

        parser.require_name(True)
        parser.allow_label(True)
        args = parser.parse_args(['name', 'label'])
        self.assertArgsEqual(args, name='name', label='label')

    def test_parse_args_each_flag(self):
        parser = ArgParser('test_parse_args_each_flag')

        args = parser.parse_args(['--debug', 'name'])
        self.assertArgsEqual(args, debug=True, name='name')

        args = parser.parse_args(['--foreground', 'name'])
        self.assertArgsEqual(args, foreground=True, name='name')

        args = parser.parse_args(['--monitor', 'name'])
        self.assertArgsEqual(args, monitor=True, name='name')

        args = parser.parse_args(['--no-cipd', 'name'])
        self.assertArgsEqual(args, name='name', no_cipd=True)

        args = parser.parse_args(['--output', 'output', 'name'])
        self.assertArgsEqual(args, name='name', output='output')

        args = parser.parse_args(['--staging', 'staging', 'name'])
        self.assertArgsEqual(args, name='name', staging='staging')

    def test_parse_args_missing_value(self):
        parser = ArgParser('test_parse_args_missing_value')
        parser.require_name(False)

        with self.assertRaises(SystemExit):
            parser.parse_args(['--output'])

        with self.assertRaises(SystemExit):
            parser.parse_args(['--staging'])

    def test_parse_args_unrecognized(self):
        parser = ArgParser('test_parse_args_unrecognized')
        parser.require_name(False)

        with self.assertRaises(SystemExit):
            parser.parse_args(['--unknown'])

        with self.assertRaises(SystemExit):
            parser.parse_args(['-help=1'])

        parser.require_name(True)
        parser.allow_label(True)
        with self.assertRaises(SystemExit):
            parser.parse_args(['one', 'two', 'three'])

    def test_parse_all_flags(self):
        parser = ArgParser('test_parse_all_flags')
        parser.allow_label(True)
        args, libfuzzer_opts, libfuzzer_args, subprocess_args = parser.parse(
            [
                '--debug',
                '--foreground',
                '--monitor',
                '--no-cipd',
                '--output',
                'output',
                '--staging',
                'staging',
                'name',
                'label',
            ])
        self.assertArgsEqual(
            args,
            debug=True,
            foreground=True,
            monitor=True,
            no_cipd=True,
            output='output',
            staging='staging',
            name='name',
            label='label')
        self.assertEqual(libfuzzer_opts, {})
        self.assertEqual(libfuzzer_args, [])
        self.assertEqual(subprocess_args, [])

    def test_parse(self):
        parser = ArgParser('test_parse')
        args, libfuzzer_opts, libfuzzer_args, subprocess_args = parser.parse(
            [
                '--debug',
                '-foo=twas',
                '-bar=bryllyg',
                'name',
                '-device="and the"',
                'corpus/',
                '--',
                '-foo=slythy',
                'toves',
                '--debug',
            ])
        self.assertArgsEqual(args, debug=True, name='name')
        self.assertEqual(
            libfuzzer_opts, {
                'foo': 'twas',
                'bar': 'bryllyg',
                'device': '"and the"',
            })
        self.assertEqual(libfuzzer_args, ['corpus/'])
        self.assertEqual(subprocess_args, ['-foo=slythy', 'toves', '--debug'])


if __name__ == '__main__':
    unittest.main()
