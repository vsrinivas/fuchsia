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
        parser = rustc_remote_wrapper.main_arg_parser()
        args = parser.parse_args()
        self.assertFalse(args.dry_run)
        self.assertFalse(args.local)
        self.assertFalse(args.verbose)
        self.assertFalse(args.fsatrace)
        self.assertEqual(args.command, [])

    def testDefaultOpposites(self):
        parser = rustc_remote_wrapper.main_arg_parser()
        args = parser.parse_args(
            [
                '--dry-run', '--local', '--verbose', '--fsatrace', '--',
                'rustc', 'src/lib.rs'
            ])
        self.assertTrue(args.dry_run)
        self.assertTrue(args.local)
        self.assertTrue(args.verbose)
        self.assertTrue(args.fsatrace)
        self.assertEqual(args.command, ['rustc', 'src/lib.rs'])


if __name__ == '__main__':
    unittest.main()
