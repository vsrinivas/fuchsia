#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest

import test_env
from lib.args import ArgParser
from collections import namedtuple


class TestArgParser(unittest.TestCase):

    # Unit test assertions

    def assertArgsEqual(
            self,
            args,
            debug=False,
            foreground=False,
            monitor=False,
            name=None,
            output=None):
        self.assertEqual(args.debug, debug)
        self.assertEqual(args.foreground, foreground)
        self.assertEqual(args.monitor, monitor)
        self.assertEqual(args.name, name)
        self.assertEqual(args.output, output)

    # Unit tests

    def test_parse_name(self):
        parser = ArgParser('test_parse_name')

        # By default, 'name' is required
        parser.require_name(True)
        with self.assertRaises(SystemExit):
            parser.parse([])

        args = parser.parse(['name'])
        self.assertArgsEqual(args, name='name')

        # 'name' can be made optional.
        parser.require_name(False)
        args = parser.parse([])
        self.assertArgsEqual(args)

        args = parser.parse(['name'])
        self.assertArgsEqual(args, name='name')

    def test_parse_each_flag(self):
        parser = ArgParser('test_parse_each_flag')

        args = parser.parse(['--debug', 'name'])
        self.assertArgsEqual(args, debug=True, name='name')

        args = parser.parse(['--foreground', 'name'])
        self.assertArgsEqual(args, foreground=True, name='name')

        args = parser.parse(['--monitor', 'name'])
        self.assertArgsEqual(args, monitor=True, name='name')

        args = parser.parse(['--output', 'output', 'name'])
        self.assertArgsEqual(args, name='name', output='output')

    def test_parse_missing_value(self):
        parser = ArgParser('test_parse_missing_value')
        parser.require_name(False)

        with self.assertRaises(SystemExit):
            parser.parse(['--output'])

    def test_parse_libfuzzer_inputs(self):
        parser = ArgParser('test_parse_libfuzzer_inputs')
        parser.require_name(True)
        parser = ArgParser('test_parse_all_flags')
        args = parser.parse(
            [
                'name',
                'unit1',
                '-help=1',
                'unit2',
                '--',
                'sub',
                '-sub=val',
                '--sub',
            ])
        self.assertEqual(args.libfuzzer_opts, {'help': '1'})
        self.assertEqual(args.libfuzzer_inputs, ['unit1', 'unit2'])
        self.assertEqual(args.subprocess_args, ['sub', '-sub=val', '--sub'])

    def test_parse_unrecognized(self):
        parser = ArgParser('test_parse_unrecognized')
        parser.require_name(False)

        with self.assertRaises(SystemExit):
            parser.parse(['--unknown'])

        with self.assertRaises(SystemExit):
            parser.parse(['-unknown'])

    def test_parse_all_flags(self):
        parser = ArgParser('test_parse_all_flags')
        args = parser.parse(
            [
                '--debug',
                '--foreground',
                '--monitor',
                '--output',
                'output',
                'name',
            ])
        self.assertArgsEqual(
            args,
            debug=True,
            foreground=True,
            monitor=True,
            output='output',
            name='name')
        self.assertEqual(args.libfuzzer_opts, {})
        self.assertEqual(args.libfuzzer_inputs, [])
        self.assertEqual(args.subprocess_args, [])

    def test_parse(self):
        parser = ArgParser('test_parse')
        args = parser.parse(
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
            args.libfuzzer_opts, {
                'foo': 'twas',
                'bar': 'bryllyg',
                'device': '"and the"',
            })
        self.assertEqual(args.libfuzzer_inputs, ['corpus/'])
        self.assertEqual(
            args.subprocess_args, ['-foo=slythy', 'toves', '--debug'])


if __name__ == '__main__':
    unittest.main()
