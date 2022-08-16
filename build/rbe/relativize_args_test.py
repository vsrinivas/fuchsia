#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest
from unittest import mock

import relativize_args


class SplitTransformJoinTest(unittest.TestCase):

    def test_no_change(self):
        self.assertEqual(
            relativize_args.split_transform_join('text', '=', lambda x: x),
            'text')

    def test_repeat(self):
        self.assertEqual(
            relativize_args.split_transform_join('text', '=', lambda x: x + x),
            'texttext')

    def test_with_split(self):
        self.assertEqual(
            relativize_args.split_transform_join('a=b', '=', lambda x: x + x),
            'aa=bb')

    def test_with_split_recorded(self):
        renamed_tokens = {}

        def recorded_transform(x):
            new_text = x + x
            renamed_tokens[x] = new_text
            return new_text

        self.assertEqual(
            relativize_args.split_transform_join(
                'a=b', '=', recorded_transform), 'aa=bb')
        self.assertEqual(renamed_tokens, {'a': 'aa', 'b': 'bb'})


class LexicallyRewriteTokenTest(unittest.TestCase):

    def test_repeat_text(self):
        self.assertEqual(
            relativize_args.lexically_rewrite_token('foo', lambda x: x + x),
            'foofoo')

    def test_delimters_only(self):
        self.assertEqual(
            relativize_args.lexically_rewrite_token(
                ',,==,=,=,', lambda x: x + x), ',,==,=,=,')

    def test_flag_with_value(self):

        def transform(x):
            if x.startswith('file'):
                return 'tmp-' + x
            else:
                return x

        self.assertEqual(
            relativize_args.lexically_rewrite_token('--foo=file1', transform),
            '--foo=tmp-file1')
        self.assertEqual(
            relativize_args.lexically_rewrite_token(
                'notfile,file1,file2,notfile', transform),
            'notfile,tmp-file1,tmp-file2,notfile')
        self.assertEqual(
            relativize_args.lexically_rewrite_token(
                '--foo=file1,file2', transform), '--foo=tmp-file1,tmp-file2')


class RelativizePathTest(unittest.TestCase):

    def test_abspath(self):
        self.assertEqual(
            relativize_args.relativize_path("/a/b/c", "/a/d"), "../b/c")

    def test_relpath(self):
        self.assertEqual(
            relativize_args.relativize_path("x/y/z", "/a/d"), "x/y/z")

    def test_cxx_Iflag(self):
        self.assertEqual(
            relativize_args.relativize_path("-I/j/k", "/j/a/d"), "-I../../k")

    def test_cxx_Lflag(self):
        self.assertEqual(
            relativize_args.relativize_path("-L/p/q/r", "/p/q/z"), "-L../r")

    def test_cxx_isystemflag(self):
        self.assertEqual(
            relativize_args.relativize_path("-isystem/r/s/t", "/r/v/w"),
            "-isystem../../s/t")


class RelativizeCommandTest(unittest.TestCase):

    def test_no_transform(self):
        self.assertEqual(
            relativize_args.relativize_command(
                ["echo", "hello"], "/home/sweet/home"), ["echo", "hello"])

    def test_with_env(self):
        self.assertEqual(
            relativize_args.relativize_command(
                ["HOME=/home", "echo"], "/home/subdir"),
            ["/usr/bin/env", "HOME=..", "echo"])

    def test_relativize(self):
        self.assertEqual(
            relativize_args.relativize_command(
                ["cat", "/meow/foo.txt"], "/meow/subdir"),
            ["cat", "../foo.txt"])


class MainArgParserTest(unittest.TestCase):

    def test_no_flags(self):
        parser = relativize_args.main_arg_parser()
        args = parser.parse_args([])
        self.assertFalse(args.verbose)
        self.assertFalse(args.dry_run)
        self.assertTrue(args.enable)

    def test_verbose(self):
        parser = relativize_args.main_arg_parser()
        args = parser.parse_args(["--verbose"])
        self.assertTrue(args.verbose)

    def test_dry_run(self):
        parser = relativize_args.main_arg_parser()
        args = parser.parse_args(["--dry-run"])
        self.assertTrue(args.dry_run)

    def test_disable(self):
        parser = relativize_args.main_arg_parser()
        args = parser.parse_args(["--disable"])
        self.assertFalse(args.enable)

    def test_cwd(self):
        parser = relativize_args.main_arg_parser()
        args = parser.parse_args(["--cwd", "/home/foo"])
        self.assertEqual(args.cwd, "/home/foo")

    def test_command(self):
        parser = relativize_args.main_arg_parser()
        args = parser.parse_args(["--", "echo", "bye"])
        self.assertEqual(args.command, ["echo", "bye"])


if __name__ == '__main__':
    unittest.main()
