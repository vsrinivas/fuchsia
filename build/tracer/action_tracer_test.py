#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import unittest
from unittest import mock

from typing import AbstractSet, Iterable

import action_tracer


class DepEdgesParseTests(unittest.TestCase):

    def test_invalid_input(self):
        with self.assertRaises(ValueError):
            action_tracer.parse_dep_edges(
                "output.txt input1.txt")  # missing ":"

    def test_output_only(self):
        dep = action_tracer.parse_dep_edges("output.txt:")
        self.assertEqual(dep.ins, set())
        self.assertEqual(dep.outs, {"output.txt"})

    def test_output_with_one_input(self):
        dep = action_tracer.parse_dep_edges("output.txt:input.cc")
        self.assertEqual(dep.ins, {"input.cc"})
        self.assertEqual(dep.outs, {"output.txt"})

    def test_output_with_multiple_inputs(self):
        dep = action_tracer.parse_dep_edges(
            "output.txt:input.cc includes/header.h")
        self.assertEqual(dep.ins, {"input.cc", "includes/header.h"})
        self.assertEqual(dep.outs, {"output.txt"})

    def test_output_with_multiple_inputs_unusual_spacing(self):
        dep = action_tracer.parse_dep_edges(
            "  output.txt  :    input.cc   includes/header.h  ")
        self.assertEqual(dep.ins, {"input.cc", "includes/header.h"})
        self.assertEqual(dep.outs, {"output.txt"})

    def test_file_name_with_escaped_space(self):
        dep = action_tracer.parse_dep_edges(
            "output.txt:  source\\ input.cc includes/header.h")
        self.assertEqual(dep.ins, {"source input.cc", "includes/header.h"})
        self.assertEqual(dep.outs, {"output.txt"})


class ParseDepFileTests(unittest.TestCase):

    def test_empty(self):
        depfile = action_tracer.parse_depfile([])
        self.assertEqual(depfile.deps, [])
        self.assertEqual(depfile.all_ins, set())
        self.assertEqual(depfile.all_outs, set())

    def test_two_deps(self):
        depfile = action_tracer.parse_depfile([
            "A: B",
            "C: D E",
        ])
        self.assertEqual(
            depfile.deps, [
                action_tracer.DepEdges(ins={"B"}, outs={"A"}),
                action_tracer.DepEdges(ins={"D", "E"}, outs={"C"}),
            ])
        self.assertEqual(depfile.all_ins, {"B", "D", "E"})
        self.assertEqual(depfile.all_outs, {"A", "C"})


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


class CheckAccessPermissionsTests(unittest.TestCase):

    def test_no_accesses(self):
        self.assertEqual(
            action_tracer.check_access_permissions([]),
            [],
        )

    def test_ok_read(self):
        self.assertEqual(
            action_tracer.check_access_permissions(
                [action_tracer.Read("readable.txt")],
                allowed_reads={"readable.txt"}),
            [],
        )

    def test_forbidden_read(self):
        read = action_tracer.Read("unreadable.txt")
        self.assertEqual(
            action_tracer.check_access_permissions([read]),
            [read],
        )

    def test_ok_write(self):
        self.assertEqual(
            action_tracer.check_access_permissions(
                [action_tracer.Write("writeable.txt")],
                allowed_writes={"writeable.txt"}),
            [],
        )

    def test_forbidden_writes(self):
        # make sure multiple violations accumulate
        bad_writes = [
            action_tracer.Write("unwriteable.txt"),
            action_tracer.Write("you-shall-not-pass.txt"),
        ]
        self.assertEqual(
            action_tracer.check_access_permissions(bad_writes),
            bad_writes,
        )


class CheckMissingWritesTests(unittest.TestCase):

    def test_no_accesses(self):
        self.assertEqual(
            action_tracer.check_missing_writes([], {}),
            {},
        )

    def test_only_reads(self):
        self.assertEqual(
            action_tracer.check_missing_writes(
                [action_tracer.Read("newspaper.pdf")],
                {},
            ),
            {},
        )

    def test_excess_write(self):
        self.assertEqual(
            action_tracer.check_missing_writes(
                [action_tracer.Write("side-effect.txt")],
                {},
            ),
            {},
        )

    def test_fulfilled_write(self):
        self.assertEqual(
            action_tracer.check_missing_writes(
                [action_tracer.Write("compiled.o")],
                {"compiled.o"},
            ),
            set(),
        )

    def test_missing_write(self):
        self.assertEqual(
            action_tracer.check_missing_writes(
                [],
                {"write-me.out"},
            ),
            {"write-me.out"},
        )

    def test_missing_and_fulfilled_write(self):
        self.assertEqual(
            action_tracer.check_missing_writes(
                [action_tracer.Write("compiled.o")],
                {
                    "write-me.out",
                    "compiled.o",
                },
            ),
            {"write-me.out"},
        )

    def test_written_then_deleted(self):
        self.assertEqual(
            action_tracer.check_missing_writes(
                [
                    action_tracer.Write("compiled.o"),
                    action_tracer.Delete("compiled.o"),
                ],
                {"compiled.o"},
            ),
            {"compiled.o"},
        )

    def test_deleted_then_written(self):
        self.assertEqual(
            action_tracer.check_missing_writes(
                [
                    action_tracer.Delete("compiled.o"),
                    action_tracer.Write("compiled.o"),
                ],
                {"compiled.o"},
            ),
            set(),
        )


def abspaths(container: Iterable[str]) -> AbstractSet[str]:
    return {os.path.abspath(f) for f in container}


class AccessConstraintsTests(unittest.TestCase):

    def test_empty_action(self):
        action = action_tracer.Action(script="script.sh")
        self.assertEqual(
            action.access_constraints(),
            action_tracer.AccessConstraints(
                allowed_reads=abspaths({"script.sh"})))

    def test_have_inputs(self):
        action = action_tracer.Action(
            script="script.sh", inputs=["input.txt", "main.cc"])
        self.assertEqual(
            action.access_constraints(),
            action_tracer.AccessConstraints(
                allowed_reads=abspaths({"script.sh", "input.txt", "main.cc"})))

    def test_have_outputs(self):
        action = action_tracer.Action(script="script.sh", outputs=["main.o"])
        self.assertEqual(
            action.access_constraints(),
            action_tracer.AccessConstraints(
                allowed_reads=abspaths({"script.sh", "main.o"}),
                allowed_writes=abspaths({"main.o"}),
                required_writes=abspaths({"main.o"})))

    def test_have_sources(self):
        action = action_tracer.Action(
            script="script.sh", sources=["input.src", "main.h"])
        self.assertEqual(
            action.access_constraints(),
            action_tracer.AccessConstraints(
                allowed_reads=abspaths({"script.sh", "input.src", "main.h"})))

    def test_have_response_file(self):
        action = action_tracer.Action(
            script="script.sh", response_file_name="response.out")
        self.assertEqual(
            action.access_constraints(),
            action_tracer.AccessConstraints(
                allowed_reads=abspaths({"script.sh", "response.out"})))

    def test_have_depfile(self):
        action = action_tracer.Action(script="script.sh", depfile="foo.d")
        with mock.patch.object(os.path, 'exists',
                               return_value=True) as mock_exists:
            with mock.patch("builtins.open", mock.mock_open(
                    read_data="foo.o: foo.cc foo.h\n")) as mock_file:
                constraints = action.access_constraints()

        self.assertEqual(
            constraints,
            action_tracer.AccessConstraints(
                allowed_reads=abspaths(
                    {"script.sh", "foo.d", "foo.o", "foo.cc", "foo.h"}),
                allowed_writes=abspaths({"foo.d", "foo.o", "foo.cc", "foo.h"})))


class MainArgParserTests(unittest.TestCase):

    # These args are required, and there's nothing interesting about them to test.
    required_args = "--script s.sh --trace-output t.out --label //pkg:tgt "

    def test_only_required_args(self):
        parser = action_tracer.main_arg_parser()
        args = parser.parse_args(self.required_args.split())
        self.assertEqual(args.script, "s.sh")
        self.assertEqual(args.trace_output, "t.out")
        self.assertEqual(args.label, "//pkg:tgt")
        # Make sure all checks are enabled by default
        self.assertTrue(args.check_access_permissions)
        self.assertTrue(args.check_output_freshness)

    def test_check_access_permissions(self):
        parser = action_tracer.main_arg_parser()
        args = parser.parse_args(
            (self.required_args + "--check-access-permissions").split())
        self.assertTrue(args.check_access_permissions)

    def test_no_check_access_permissions(self):
        parser = action_tracer.main_arg_parser()
        args = parser.parse_args(
            (self.required_args + "--no-check-access-permissions").split())
        self.assertFalse(args.check_access_permissions)

    def test_check_output_freshness(self):
        parser = action_tracer.main_arg_parser()
        args = parser.parse_args(
            (self.required_args + "--check-output-freshness").split())
        self.assertTrue(args.check_output_freshness)

    def test_no_check_output_freshness(self):
        parser = action_tracer.main_arg_parser()
        args = parser.parse_args(
            (self.required_args + "--no-check-output-freshness").split())
        self.assertFalse(args.check_output_freshness)


if __name__ == '__main__':
    unittest.main()
