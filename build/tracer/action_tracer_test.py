#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from unittest import mock

import action_tracer


class CheckAccessLineTests(unittest.TestCase):

    def test_allowed_read(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "r", "foo.txt", allowed_reads={"foo.txt"}, allowed_writes={}),
            [])

    def test_forbiddden_read(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "r", "bar.txt", allowed_reads={}, allowed_writes={}),
            [("read", "bar.txt")])

    def test_allowed_write(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "w", "foo.txt", allowed_reads={}, allowed_writes={"foo.txt"}),
            [])

    def test_forbiddden_write(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "w", "baz.txt", allowed_reads={}, allowed_writes={}),
            [("write", "baz.txt")])

    def test_allowed_touch(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "t", "foo.txt", allowed_reads={}, allowed_writes={"foo.txt"}),
            [])

    def test_forbiddden_touch(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "t", "baz.txt", allowed_reads={}, allowed_writes={}),
            [("write", "baz.txt")])

    def test_allowed_delete(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "d", "foo.txt", allowed_reads={}, allowed_writes={"foo.txt"}),
            [])

    def test_forbiddden_delete(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "d", "baz.txt", allowed_reads={}, allowed_writes={}),
            [("write", "baz.txt")])

    def test_allowed_move(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "m",
                "dest|source",
                allowed_reads={},
                allowed_writes={"source", "dest"}), [])

    def test_forbidden_move_from_source(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "m", "dest|source", allowed_reads={}, allowed_writes={"dest"}),
            [("write", "source")])

    def test_forbidden_move_to_dest(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "m", "dest|source", allowed_reads={},
                allowed_writes={"source"}), [("write", "dest")])

    def test_forbidden_move_source_and_dest(self):
        self.assertEqual(
            action_tracer.check_access_line(
                "m", "dest|source", allowed_reads={}, allowed_writes={}),
            [("write", "dest"), ("write", "source")])


# No required prefix, no ignore affixes, no allowed accesses.
_default_checker = action_tracer.AccessTraceChecker(
    required_path_prefix="",
    ignored_prefixes={},
    ignored_suffixes={},
    allowed_reads={},
    allowed_writes={})


class AccessTraceCheckerTests(unittest.TestCase):

    def test_no_accesses(self):
        self.assertEqual(
            _default_checker.check_accesses([]),
            [],
        )

    def test_ignore_invalid_trace_line(self):
        self.assertEqual(
            _default_checker.check_accesses(["invalid-trace-desc"]),
            [],
        )

    def test_ok_read(self):
        checker = action_tracer.AccessTraceChecker(
            allowed_reads={"readable.txt"})
        self.assertEqual(
            checker.check_accesses(["r|readable.txt"]),
            [],
        )

    def test_forbidden_read(self):
        self.assertEqual(
            _default_checker.check_accesses(["r|unreadable.txt"]),
            [("read", "unreadable.txt")],
        )

    def test_ignore_read_outside_of_prefix(self):
        checker = action_tracer.AccessTraceChecker(
            required_path_prefix="/home/project")
        self.assertEqual(
            checker.check_accesses(["r|/elsewhere/file.txt"]),
            [],
        )

    def test_ok_read_inside_of_prefix(self):
        checker = action_tracer.AccessTraceChecker(
            required_path_prefix="/home/project",
            allowed_reads={"/home/project/file.txt"})
        self.assertEqual(
            checker.check_accesses(["r|/home/project/file.txt"]),
            [],
        )

    def test_ok_write(self):
        checker = action_tracer.AccessTraceChecker(
            allowed_writes={"writeable.txt"})
        self.assertEqual(
            checker.check_accesses(["w|writeable.txt"]),
            [],
        )

    def test_forbidden_writes(self):
        # make sure multiple violations accumulate
        self.assertEqual(
            _default_checker.check_accesses(
                [
                    "w|unwriteable.txt",
                    "w|you-shall-not-pass.txt",
                ]),
            [
                ("write", "unwriteable.txt"),
                ("write", "you-shall-not-pass.txt"),
            ],
        )

    def test_ignore_write_outside_of_prefix(self):
        checker = action_tracer.AccessTraceChecker(
            required_path_prefix="/home/project")
        self.assertEqual(
            checker.check_accesses(["w|/elsewhere/file.txt"]),
            [],
        )

    def test_ok_write_inside_of_prefix(self):
        checker = action_tracer.AccessTraceChecker(
            required_path_prefix="/home/project",
            allowed_writes={"/home/project/file.out"})
        self.assertEqual(
            checker.check_accesses(["w|/home/project/file.out"]),
            [],
        )

    def test_ignore_write_prefix(self):
        # Make sure this fails without an ignored_prefix.
        self.assertEqual(
            _default_checker.check_accesses(
                ["w|/tmp/intermediate_calculation.txt"]),
            [("write", "/tmp/intermediate_calculation.txt")],
        )
        # Make sure this passes with an ignored_prefix.
        checker = action_tracer.AccessTraceChecker(ignored_prefixes={"/tmp/"})
        self.assertEqual(
            checker.check_accesses(["w|/tmp/intermediate_calculation.txt"]),
            [],
        )

    def test_ignore_write_suffix(self):
        # Make sure this fails without an ignored_prefix.
        self.assertEqual(
            _default_checker.check_accesses(
                ["w|/home/project/out/preprocessed.ii"]),
            [("write", "/home/project/out/preprocessed.ii")],
        )
        # Make sure this passes with an ignored_prefix.
        checker = action_tracer.AccessTraceChecker(
            # e.g. from compiling with --save-temps
            ignored_suffixes={".ii"})
        self.assertEqual(
            checker.check_accesses(["w|/home/project/out/preprocessed.ii"]),
            [],
        )

    def test_ignore_write_infix(self):
        # Make sure this fails without ignored_path_parts.
        self.assertEqual(
            _default_checker.check_accesses(
                ["w|/home/project/out/__tmp__/preprocessed.ii"]),
            [("write", "/home/project/out/__tmp__/preprocessed.ii")],
        )
        # Make sure this passes with ignored_path_parts.
        checker = action_tracer.AccessTraceChecker(
            # e.g. from compiling with --save-temps
            ignored_path_parts={"__tmp__"})
        self.assertEqual(
            checker.check_accesses(
                ["w|/home/project/out/__tmp__/preprocessed.ii"]),
            [],
        )


if __name__ == '__main__':
    unittest.main()
