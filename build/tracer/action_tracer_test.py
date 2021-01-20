#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from unittest import mock

import action_tracer


class ParseFsatraceOutputTests(unittest.TestCase):

    def test_empty_stream(self):
        self.assertEqual(
            list(action_tracer.parse_fsatrace_output([])),
            [],
        )

    def test_ignore_malformed_line(self):
        self.assertEqual(
            list(action_tracer.parse_fsatrace_output(["invalid_line"])),
            [],
        )

    def test_read(self):
        self.assertEqual(
            list(action_tracer.parse_fsatrace_output(["r|README.md"])),
            [action_tracer.Read("README.md")],
        )

    def test_write(self):
        self.assertEqual(
            list(action_tracer.parse_fsatrace_output(["w|main.o"])),
            [action_tracer.Write("main.o")],
        )

    def test_touch(self):
        self.assertEqual(
            list(action_tracer.parse_fsatrace_output(["t|file.stamp"])),
            [action_tracer.Write("file.stamp")],
        )

    def test_delete(self):
        self.assertEqual(
            list(action_tracer.parse_fsatrace_output(["d|remove-me.tmp"])),
            [action_tracer.Delete("remove-me.tmp")],
        )

    def test_move(self):
        self.assertEqual(
            list(
                action_tracer.parse_fsatrace_output(["m|dest.txt|source.txt"])),
            [
                action_tracer.Delete("source.txt"),
                action_tracer.Write("dest.txt"),
            ],
        )

    def test_sequence(self):
        self.assertEqual(
            list(
                action_tracer.parse_fsatrace_output(
                    [
                        "m|dest.txt|source.txt",
                        "r|input.txt",
                        "w|output.log",
                    ])),
            [
                action_tracer.Delete("source.txt"),
                action_tracer.Write("dest.txt"),
                action_tracer.Read("input.txt"),
                action_tracer.Write("output.log"),
            ],
        )


class AccessShouldCheckTests(unittest.TestCase):

    def test_no_required_prefix(self):
        self.assertTrue(
            action_tracer.Read("book").should_check(required_path_prefix=""))
        self.assertTrue(
            action_tracer.Write("block").should_check(required_path_prefix=""))

    def test_required_prefix_matches(self):
        prefix = "/home/project"
        self.assertTrue(
            action_tracer.Read("/home/project/book").should_check(
                required_path_prefix=prefix))
        self.assertTrue(
            action_tracer.Write("/home/project/out/block").should_check(
                required_path_prefix=prefix))

    def test_required_prefix_no_match(self):
        prefix = "/home/project"
        self.assertFalse(
            action_tracer.Read("book").should_check(
                required_path_prefix=prefix))
        self.assertFalse(
            action_tracer.Write("output/log").should_check(
                required_path_prefix=prefix))

    def test_no_ignored_prefix(self):
        self.assertTrue(
            action_tracer.Read("book").should_check(ignored_prefixes={}))
        self.assertTrue(
            action_tracer.Write("output/log").should_check(ignored_prefixes={}))

    def test_ignored_prefix_matches(self):
        prefixes = {"/tmp"}
        self.assertFalse(
            action_tracer.Read("/tmp/book").should_check(
                ignored_prefixes=prefixes))
        self.assertFalse(
            action_tracer.Write("/tmp/log").should_check(
                ignored_prefixes=prefixes))

    def test_ignored_prefix_no_match(self):
        prefixes = {"/tmp", "/no/look/here"}
        self.assertTrue(
            action_tracer.Read("book").should_check(ignored_prefixes=prefixes))
        self.assertTrue(
            action_tracer.Write("out/log").should_check(
                ignored_prefixes=prefixes))

    def test_no_ignored_suffix(self):
        self.assertTrue(
            action_tracer.Read("book").should_check(ignored_suffixes={}))
        self.assertTrue(
            action_tracer.Write("output/log").should_check(ignored_suffixes={}))

    def test_ignored_suffix_matches(self):
        suffixes = {".ii"}  # e.g. from compiler --save-temps
        self.assertFalse(
            action_tracer.Read("book.ii").should_check(
                ignored_suffixes=suffixes))
        self.assertFalse(
            action_tracer.Write("tmp/log.ii").should_check(
                ignored_suffixes=suffixes))

    def test_ignored_suffix_no_match(self):
        suffixes = {".ii", ".S"}  # e.g. from compiler --save-temps
        self.assertTrue(
            action_tracer.Read("book.txt").should_check(
                ignored_suffixes=suffixes))
        self.assertTrue(
            action_tracer.Write("out/process.log").should_check(
                ignored_suffixes=suffixes))

    def test_ignored_path_components_no_match(self):
        components = {"__auto__", ".generated"}
        self.assertTrue(
            action_tracer.Read("book").should_check(
                ignored_path_parts=components))
        self.assertTrue(
            action_tracer.Write("out/log").should_check(
                ignored_path_parts=components))

    def test_ignored_path_components_matches(self):
        components = {"__auto__", ".generated"}
        self.assertFalse(
            action_tracer.Read("library/__auto__/book").should_check(
                ignored_path_parts=components))
        self.assertFalse(
            action_tracer.Write(".generated/out/log").should_check(
                ignored_path_parts=components))


class CheckAccessAllowedTests(unittest.TestCase):

    def test_allowed_read(self):
        self.assertTrue(
            action_tracer.Read("foo.txt").allowed(
                allowed_reads={"foo.txt"}, allowed_writes={}))

    def test_forbiddden_read(self):
        self.assertFalse(
            action_tracer.Read("bar.txt").allowed(
                allowed_reads={}, allowed_writes={}))

    def test_allowed_write(self):
        self.assertTrue(
            action_tracer.Write("foo.txt").allowed(
                allowed_reads={}, allowed_writes={"foo.txt"}))

    def test_forbiddden_write(self):
        self.assertFalse(
            action_tracer.Write("baz.txt").allowed(
                allowed_reads={}, allowed_writes={}))

    def test_allowed_delete(self):
        self.assertTrue(
            action_tracer.Delete("foo.txt").allowed(
                allowed_reads={}, allowed_writes={"foo.txt"}))

    def test_forbiddden_delete(self):
        self.assertFalse(
            action_tracer.Delete("baz.txt").allowed(
                allowed_reads={}, allowed_writes={}))


# No required prefix, no ignore affixes, no allowed accesses.
_default_checker = action_tracer.AccessTraceChecker(
    allowed_reads={}, allowed_writes={})


class AccessTraceCheckerTests(unittest.TestCase):

    def test_no_accesses(self):
        self.assertEqual(
            _default_checker.check_accesses([]),
            [],
        )

    def test_ok_read(self):
        checker = action_tracer.AccessTraceChecker(
            allowed_reads={"readable.txt"})
        self.assertEqual(
            checker.check_accesses([action_tracer.Read("readable.txt")]),
            [],
        )

    def test_forbidden_read(self):
        read = action_tracer.Read("unreadable.txt")
        self.assertEqual(
            _default_checker.check_accesses([read]),
            [read],
        )

    def test_ok_write(self):
        checker = action_tracer.AccessTraceChecker(
            allowed_writes={"writeable.txt"})
        self.assertEqual(
            checker.check_accesses([action_tracer.Write("writeable.txt")]),
            [],
        )

    def test_forbidden_writes(self):
        # make sure multiple violations accumulate
        bad_writes = [
            action_tracer.Write("unwriteable.txt"),
            action_tracer.Write("you-shall-not-pass.txt"),
        ]
        self.assertEqual(
            _default_checker.check_accesses(bad_writes),
            bad_writes,
        )


if __name__ == '__main__':
    unittest.main()
