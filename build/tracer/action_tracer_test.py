#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import textwrap
import unittest
from unittest import mock

from typing import AbstractSet, Iterable

import action_tracer


class ToolCommandTests(unittest.TestCase):

    def test_empty_command(self):
        with self.assertRaises(IndexError):
            action_tracer.ToolCommand().tool

    def test_command_no_args(self):
        command = action_tracer.ToolCommand(tokens=['echo'])
        self.assertEqual(command.env_tokens, [])
        self.assertEqual(command.tool, 'echo')
        self.assertEqual(command.args, [])

    def test_command_with_env(self):
        command = action_tracer.ToolCommand(tokens=['TMPDIR=/my/tmp', 'echo'])
        self.assertEqual(command.env_tokens, ['TMPDIR=/my/tmp'])
        self.assertEqual(command.tool, 'echo')
        self.assertEqual(command.args, [])

    def test_command_with_args(self):
        command = action_tracer.ToolCommand(
            tokens=['ln', '-f', '-s', 'foo', 'bar'])
        self.assertEqual(command.env_tokens, [])
        self.assertEqual(command.tool, 'ln')
        self.assertEqual(command.args, ['-f', '-s', 'foo', 'bar'])

    def test_unwrap_no_change(self):
        tokens = ['ln', '-f', '-s', 'foo', 'bar']
        command = action_tracer.ToolCommand(tokens=tokens)
        self.assertEqual(command.unwrap().tokens, tokens)

    def test_unwrap_one_level(self):
        tokens = ['wrapper', '--opt', 'foo', '--', 'bar.sh', 'arg']
        command = action_tracer.ToolCommand(tokens=tokens)
        self.assertEqual(command.unwrap().tokens, ['bar.sh', 'arg'])

    def test_unwrap_one_of_many_level(self):
        tokens = [
            'wrapper', '--opt', 'foo', '--', 'bar.sh', 'arg', '--', 'inner.sh'
        ]
        command = action_tracer.ToolCommand(tokens=tokens)
        command2 = command.unwrap()
        self.assertEqual(command2.tokens, ['bar.sh', 'arg', '--', 'inner.sh'])
        command3 = command2.unwrap()
        self.assertEqual(command3.tokens, ['inner.sh'])


class IsKnownWrapperTests(unittest.TestCase):

    def test_action_tracer_is_not_wrapper(self):
        command = action_tracer.ToolCommand(
            tokens=[
                'path/to/python3.x', 'path/to/not_a_wrapper.py', '--opt1',
                'arg1'
            ])
        self.assertFalse(action_tracer.is_known_wrapper(command))

    def test_action_tracer_is_not_wrapper_implicit_interpreter(self):
        command = action_tracer.ToolCommand(
            tokens=['path/to/not_a_wrapper.py', '--opt1', 'arg1'])
        self.assertFalse(action_tracer.is_known_wrapper(command))

    def test_action_tracer_is_wrapper(self):
        command = action_tracer.ToolCommand(
            tokens=[
                'path/to/python3.x', 'path/to/action_tracer.py', '--', 'foo.sh',
                'arg1', 'arg2'
            ])
        self.assertTrue(action_tracer.is_known_wrapper(command))

    def test_action_tracer_is_wrapper_extra_python_flag(self):
        command = action_tracer.ToolCommand(
            tokens=[
                'path/to/python3.x', '-S', 'path/to/action_tracer.py', '--',
                'foo.sh', 'arg1', 'arg2'
            ])
        self.assertTrue(action_tracer.is_known_wrapper(command))

    def test_action_tracer_is_wrapper_implicit_interpreter(self):
        command = action_tracer.ToolCommand(
            tokens=['path/to/action_tracer.py', '--', 'foo.sh', 'arg1', 'arg2'])
        self.assertTrue(action_tracer.is_known_wrapper(command))


class DepEdgesParseTests(unittest.TestCase):

    def test_invalid_input(self):
        with self.assertRaises(ValueError):
            action_tracer.parse_dep_edges(
                "output.txt input1.txt")  # missing ":"

    def test_output_only(self):
        dep = action_tracer.parse_dep_edges("output.txt:")
        self.assertEqual(dep.ins, set())
        self.assertEqual(dep.outs, {"output.txt"})

    def test_multipl_outputs_only(self):
        dep = action_tracer.parse_dep_edges("output.txt  output2.txt :")
        self.assertEqual(dep.ins, set())
        self.assertEqual(dep.outs, {"output.txt", "output2.txt"})

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

    def test_multiple_ins_multiple_outs(self):
        depfile = action_tracer.parse_depfile(["a b: c d"])
        self.assertEqual(
            depfile.deps, [
                action_tracer.DepEdges(ins={"c", "d"}, outs={"a", "b"}),
            ])
        self.assertEqual(depfile.all_ins, {"c", "d"})
        self.assertEqual(depfile.all_outs, {"a", "b"})

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

    def test_continuation(self):
        depfile = action_tracer.parse_depfile(
            [
                "a \\\n",
                "b: \\\n",
                "c \\\n",
                "d",
            ])
        self.assertEqual(
            depfile.deps, [
                action_tracer.DepEdges(ins={"c", "d"}, outs={"a", "b"}),
            ])
        self.assertEqual(depfile.all_ins, {"c", "d"})
        self.assertEqual(depfile.all_outs, {"a", "b"})

    def test_carriage_continuation(self):
        depfile = action_tracer.parse_depfile(
            [
                "a \\\r\n",
                "b: c \\\r\n",
                "d e",
            ])
        self.assertEqual(
            depfile.deps, [
                action_tracer.DepEdges(ins={"c", "d", "e"}, outs={"a", "b"}),
            ])
        self.assertEqual(depfile.all_ins, {"c", "d", "e"})
        self.assertEqual(depfile.all_outs, {"a", "b"})

    def test_space_in_filename(self):
        depfile = action_tracer.parse_depfile(["a\\ b: c d"])
        self.assertEqual(
            depfile.deps, [
                action_tracer.DepEdges(ins={"c", "d"}, outs={"a b"}),
            ])
        self.assertEqual(depfile.all_ins, {"c", "d"})
        self.assertEqual(depfile.all_outs, {"a b"})

    def test_consecutive_backslashes(self):
        with self.assertRaises(ValueError):
            depfile = action_tracer.parse_depfile(["a\\\\ b: c"])

    def test_trailing_escaped_whitespace(self):
        with self.assertRaises(ValueError):
            depfile = action_tracer.parse_depfile([
                "a \\ \r\n",
                "b: c",
            ])
        with self.assertRaises(ValueError):
            depfile = action_tracer.parse_depfile([
                "a \\ \n",
                "b: c",
            ])
        with self.assertRaises(ValueError):
            depfile = action_tracer.parse_depfile([
                "a \\    \n",
                "b: c",
            ])

    def test_unfinished_line_continuation(self):
        with self.assertRaises(ValueError):
            depfile = action_tracer.parse_depfile([
                "a \\\n",
                "b: c \\\n",
            ])

    def test_blank_line(self):
        depfile = action_tracer.parse_depfile(
            [
                "a:b",
                " ",
                "b:",
            ])
        self.assertEqual(
            depfile.deps, [
                action_tracer.DepEdges(ins={"b"}, outs={"a"}),
                action_tracer.DepEdges(ins=set(), outs={"b"}),
            ])
        self.assertEqual(depfile.all_ins, {"b"})
        self.assertEqual(depfile.all_outs, {"a", "b"})

    def test_comment(self):
        depfile = action_tracer.parse_depfile(
            [
                " # a:b",
                "b:",
            ])
        self.assertEqual(
            depfile.deps, [
                action_tracer.DepEdges(ins=set(), outs={"b"}),
            ])
        self.assertEqual(depfile.all_ins, set())
        self.assertEqual(depfile.all_outs, {"b"})

    def test_continuation_blank_line(self):
        with self.assertRaises(ValueError):
            depfile = action_tracer.parse_depfile([
                "a: \\\n",
                "",
                "b",
            ])

    def test_continuation_comment(self):
        with self.assertRaises(ValueError):
            depfile = action_tracer.parse_depfile([
                "a: \\\n",
                "# comment",
                "b",
            ])


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


class MatchConditionsTests(unittest.TestCase):

    def test_no_conditions(self):
        self.assertFalse(action_tracer.MatchConditions().matches("foo/bar"))

    def test_prefix_matches(self):
        self.assertTrue(
            action_tracer.MatchConditions(prefixes={"fo"}).matches("foo/bar"))

    def test_suffix_matches(self):
        self.assertTrue(
            action_tracer.MatchConditions(suffixes={"ar"}).matches("foo/bar"))

    def test_component_matches(self):
        self.assertTrue(
            action_tracer.MatchConditions(
                components={"bar", "bq"}).matches("foo/bar/baz.txt"))


class AccessShouldCheckTests(unittest.TestCase):

    def test_no_required_prefix(self):
        ignore_conditions = action_tracer.MatchConditions()
        self.assertTrue(
            action_tracer.Read("book").should_check(
                ignore_conditions=ignore_conditions, required_path_prefix=""))
        self.assertTrue(
            action_tracer.Write("block").should_check(
                ignore_conditions=ignore_conditions, required_path_prefix=""))

    def test_required_prefix_matches(self):
        ignore_conditions = action_tracer.MatchConditions()
        prefix = "/home/project"
        self.assertTrue(
            action_tracer.Read("/home/project/book").should_check(
                ignore_conditions=ignore_conditions,
                required_path_prefix=prefix))
        self.assertTrue(
            action_tracer.Write("/home/project/out/block").should_check(
                ignore_conditions=ignore_conditions,
                required_path_prefix=prefix))

    def test_required_prefix_no_match(self):
        ignore_conditions = action_tracer.MatchConditions()
        prefix = "/home/project"
        self.assertFalse(
            action_tracer.Read("book").should_check(
                ignore_conditions=ignore_conditions,
                required_path_prefix=prefix))
        self.assertFalse(
            action_tracer.Write("output/log").should_check(
                ignore_conditions=ignore_conditions,
                required_path_prefix=prefix))

    def test_no_ignored_prefix(self):
        ignore_conditions = action_tracer.MatchConditions(prefixes={})
        self.assertTrue(
            action_tracer.Read("book").should_check(
                ignore_conditions=ignore_conditions))
        self.assertTrue(
            action_tracer.Write("output/log").should_check(
                ignore_conditions=ignore_conditions))

    def test_ignored_prefix_matches(self):
        ignore_conditions = action_tracer.MatchConditions(prefixes={"/tmp"})
        self.assertFalse(
            action_tracer.Read("/tmp/book").should_check(
                ignore_conditions=ignore_conditions))
        self.assertFalse(
            action_tracer.Write("/tmp/log").should_check(
                ignore_conditions=ignore_conditions))

    def test_ignored_prefix_no_match(self):
        ignore_conditions = action_tracer.MatchConditions(
            prefixes={"/tmp", "/no/look/here"})
        self.assertTrue(
            action_tracer.Read("book").should_check(
                ignore_conditions=ignore_conditions))
        self.assertTrue(
            action_tracer.Write("out/log").should_check(
                ignore_conditions=ignore_conditions))

    def test_no_ignored_suffix(self):
        ignore_conditions = action_tracer.MatchConditions(suffixes={})
        self.assertTrue(
            action_tracer.Read("book").should_check(
                ignore_conditions=ignore_conditions))
        self.assertTrue(
            action_tracer.Write("output/log").should_check(
                ignore_conditions=ignore_conditions))

    def test_ignored_suffix_matches(self):
        # e.g. from compiler --save-temps
        ignore_conditions = action_tracer.MatchConditions(suffixes={".ii"})
        self.assertFalse(
            action_tracer.Read("book.ii").should_check(
                ignore_conditions=ignore_conditions))
        self.assertFalse(
            action_tracer.Write("tmp/log.ii").should_check(
                ignore_conditions=ignore_conditions))

    def test_ignored_suffix_no_match(self):
        # e.g. from compiler --save-temps
        ignore_conditions = action_tracer.MatchConditions(
            suffixes={".ii", ".S"})
        self.assertTrue(
            action_tracer.Read("book.txt").should_check(
                ignore_conditions=ignore_conditions))
        self.assertTrue(
            action_tracer.Write("out/process.log").should_check(
                ignore_conditions=ignore_conditions))

    def test_ignored_path_components_no_match(self):
        ignore_conditions = action_tracer.MatchConditions(
            components={"__auto__", ".generated"})
        self.assertTrue(
            action_tracer.Read("book").should_check(
                ignore_conditions=ignore_conditions))
        self.assertTrue(
            action_tracer.Write("out/log").should_check(
                ignore_conditions=ignore_conditions))

    def test_ignored_path_components_matches(self):
        ignore_conditions = action_tracer.MatchConditions(
            components={"__auto__", ".generated"})
        self.assertFalse(
            action_tracer.Read("library/__auto__/book").should_check(
                ignore_conditions=ignore_conditions))
        self.assertFalse(
            action_tracer.Write(".generated/out/log").should_check(
                ignore_conditions=ignore_conditions))


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


class FormatAccessSetTest(unittest.TestCase):

    def test_empty(self):
        self.assertEqual(str(action_tracer.FSAccessSet()), "[empty accesses]")

    def test_reads(self):
        self.assertEqual(
            str(action_tracer.FSAccessSet(reads={"c", "a", "b"})),
            textwrap.dedent(
                """\
                 Reads:
                   a
                   b
                   c"""))

    def test_writes(self):
        self.assertEqual(
            str(action_tracer.FSAccessSet(writes={"e", "f", "d"})),
            textwrap.dedent(
                """\
                 Writes:
                   d
                   e
                   f"""))

    def test_deletes(self):
        self.assertEqual(
            str(action_tracer.FSAccessSet(deletes={"r", "q", "p"})),
            textwrap.dedent(
                """\
                 Deletes:
                   p
                   q
                   r"""))

    def test_reads_writes(self):
        files = {"c", "a", "b"}
        self.assertEqual(
            str(action_tracer.FSAccessSet(reads=files, writes=files)),
            textwrap.dedent(
                """\
                 Reads:
                   a
                   b
                   c
                 Writes:
                   a
                   b
                   c"""))

    def test_writes_deletes(self):
        files = {"c", "a", "b"}
        self.assertEqual(
            str(action_tracer.FSAccessSet(writes=files, deletes=files)),
            textwrap.dedent(
                """\
                 Writes:
                   a
                   b
                   c
                 Deletes:
                   a
                   b
                   c"""))


class FinalizeFileSystemAccessesTest(unittest.TestCase):

    def test_no_accesses(self):
        self.assertEqual(
            action_tracer.finalize_filesystem_accesses([]),
            action_tracer.FSAccessSet())

    def test_reads(self):
        self.assertEqual(
            action_tracer.finalize_filesystem_accesses(
                [
                    action_tracer.Read("r1.txt"),
                    action_tracer.Read("r2.txt"),
                ]), action_tracer.FSAccessSet(reads={"r1.txt", "r2.txt"}))

    def test_writes(self):
        self.assertEqual(
            action_tracer.finalize_filesystem_accesses(
                [
                    action_tracer.Write("wb.txt"),
                    action_tracer.Write("wa.txt"),
                ]), action_tracer.FSAccessSet(writes={"wa.txt", "wb.txt"}))

    def test_reads_writes_no_deletes(self):
        self.assertEqual(
            action_tracer.finalize_filesystem_accesses(
                [
                    action_tracer.Read("r2.txt"),
                    action_tracer.Write("wb.txt"),
                    action_tracer.Write("wa.txt"),
                    action_tracer.Read("r1.txt"),
                ]),
            action_tracer.FSAccessSet(
                reads={"r1.txt", "r2.txt"}, writes={"wa.txt", "wb.txt"}))

    def test_read_after_write(self):
        self.assertEqual(
            action_tracer.finalize_filesystem_accesses(
                [
                    action_tracer.Write("temp.txt"),
                    action_tracer.Read("temp.txt"),
                ]), action_tracer.FSAccessSet(reads=set(), writes={"temp.txt"}))

    def test_delete(self):
        self.assertEqual(
            action_tracer.finalize_filesystem_accesses(
                [
                    action_tracer.Delete("d1.txt"),
                    action_tracer.Delete("d2.txt"),
                ]), action_tracer.FSAccessSet(deletes={"d1.txt", "d2.txt"}))

    def test_delete_after_write(self):
        self.assertEqual(
            action_tracer.finalize_filesystem_accesses(
                [
                    action_tracer.Write("temp.txt"),
                    action_tracer.Delete("temp.txt"),
                ]), action_tracer.FSAccessSet())

    def test_write_after_delete(self):
        self.assertEqual(
            action_tracer.finalize_filesystem_accesses(
                [
                    action_tracer.Delete("temp.txt"),
                    action_tracer.Write("temp.txt"),
                ]), action_tracer.FSAccessSet(writes={"temp.txt"}))

    def test_write_read_delete(self):
        self.assertEqual(
            action_tracer.finalize_filesystem_accesses(
                [
                    action_tracer.Write("temp.txt"),
                    action_tracer.Read("temp.txt"),
                    action_tracer.Delete("temp.txt"),
                ]), action_tracer.FSAccessSet())


class CheckAccessPermissionsTests(unittest.TestCase):

    def test_no_accesses(self):
        self.assertEqual(
            action_tracer.check_access_permissions(
                action_tracer.FSAccessSet(), action_tracer.AccessConstraints()),
            action_tracer.FSAccessSet(),
        )

    def test_ok_read(self):
        self.assertEqual(
            action_tracer.check_access_permissions(
                action_tracer.FSAccessSet(reads={"readable.txt"}),
                action_tracer.AccessConstraints(
                    allowed_reads={"readable.txt"})),
            action_tracer.FSAccessSet(),
        )

    def test_forbidden_read(self):
        read = "unreadable.txt"
        self.assertEqual(
            action_tracer.check_access_permissions(
                action_tracer.FSAccessSet(reads={read}),
                action_tracer.AccessConstraints()),
            action_tracer.FSAccessSet(reads={read}),
        )

    def test_ok_write(self):
        self.assertEqual(
            action_tracer.check_access_permissions(
                action_tracer.FSAccessSet(writes={"writeable.txt"}),
                action_tracer.AccessConstraints(
                    allowed_writes={"writeable.txt"})),
            action_tracer.FSAccessSet(),
        )

    def test_forbidden_writes(self):
        # make sure multiple violations accumulate
        bad_writes = {
            "unwriteable.txt",
            "you-shall-not-pass.txt",
        }
        self.assertEqual(
            action_tracer.check_access_permissions(
                action_tracer.FSAccessSet(writes=bad_writes),
                action_tracer.AccessConstraints()),
            action_tracer.FSAccessSet(writes=bad_writes),
        )

    def test_read_from_temporary_writes_ok(self):
        temp_file = "__file.tmp"
        reads = {temp_file}
        writes = {
            "unwriteable.txt",
            temp_file,
        }
        self.assertEqual(
            action_tracer.check_access_permissions(
                action_tracer.FSAccessSet(reads=reads, writes=writes),
                action_tracer.AccessConstraints()),
            action_tracer.FSAccessSet(reads=set(), writes=writes),
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
        action = action_tracer.Action(inputs=["script.sh"])
        self.assertEqual(
            action.access_constraints(),
            action_tracer.AccessConstraints(
                allowed_reads=abspaths({"script.sh"})))

    def test_have_inputs(self):
        action = action_tracer.Action(
            inputs=["script.sh", "input.txt", "main.cc"])
        self.assertEqual(
            action.access_constraints(),
            action_tracer.AccessConstraints(
                allowed_reads=abspaths({"script.sh", "input.txt", "main.cc"})))

    def test_have_outputs(self):
        action = action_tracer.Action(inputs=["script.sh"], outputs=["main.o"])
        self.assertEqual(
            action.access_constraints(),
            action_tracer.AccessConstraints(
                allowed_reads=abspaths({"script.sh", "main.o"}),
                allowed_writes=abspaths({"main.o"}),
                required_writes=abspaths({"main.o"})))

    def test_have_depfile_writeable_inputs(self):
        action = action_tracer.Action(inputs=["script.sh"], depfile="foo.d")
        with mock.patch.object(os.path, 'exists',
                               return_value=True) as mock_exists:
            with mock.patch("builtins.open", mock.mock_open(
                    read_data="foo.o: foo.cc foo.h\n")) as mock_file:
                constraints = action.access_constraints(
                    writeable_depfile_inputs=True)
        mock_exists.assert_called_once()
        mock_file.assert_called_once()

        self.assertEqual(
            constraints,
            action_tracer.AccessConstraints(
                allowed_reads=abspaths(
                    {"script.sh", "foo.d", "foo.o", "foo.cc", "foo.h"}),
                allowed_writes=abspaths({"foo.d", "foo.o", "foo.cc", "foo.h"})))

    def test_have_depfile_nonwritable_inputs(self):
        action = action_tracer.Action(inputs=["script.sh"], depfile="foo.d")
        with mock.patch.object(os.path, 'exists',
                               return_value=True) as mock_exists:
            with mock.patch("builtins.open", mock.mock_open(
                    read_data="foo.o: foo.cc foo.h\n")) as mock_file:
                constraints = action.access_constraints(
                    writeable_depfile_inputs=False)
        mock_exists.assert_called_once()
        mock_file.assert_called_once()

        self.assertEqual(
            constraints,
            action_tracer.AccessConstraints(
                allowed_reads=abspaths(
                    {"script.sh", "foo.d", "foo.o", "foo.cc", "foo.h"}),
                allowed_writes=abspaths({"foo.d", "foo.o"})))

    def test_links_are_followed(self):

        def fake_realpath(s: str) -> str:
            return f'test/realpath/{s}'

        action = action_tracer.Action(inputs=["script.sh"], depfile="foo.d")
        with mock.patch.object(os.path, 'exists',
                               return_value=True) as mock_exists:
            with mock.patch("builtins.open", mock.mock_open(
                    read_data="foo.o: foo.cc foo.h\n")) as mock_file:
                with mock.patch.object(os.path, 'realpath',
                                       wraps=fake_realpath) as mock_realpath:
                    constraints = action.access_constraints(
                        writeable_depfile_inputs=False)
        mock_exists.assert_called_once()
        mock_file.assert_called_once()
        mock_realpath.assert_called()

        self.assertEqual(
            constraints,
            action_tracer.AccessConstraints(
                allowed_reads=abspaths(
                    {
                        "test/realpath/script.sh",
                        "test/realpath/foo.d",
                        "test/realpath/foo.o",
                        "test/realpath/foo.cc",
                        "test/realpath/foo.h",
                    }),
                allowed_writes=abspaths({"foo.d", "foo.o"})))

    def test_have_nonexistent_depfile(self):
        action = action_tracer.Action(depfile="foo.d")
        with mock.patch.object(os.path, 'exists',
                               return_value=False) as mock_exists:
            constraints = action.access_constraints()
        mock_exists.assert_called()
        self.assertEqual(
            constraints,
            action_tracer.AccessConstraints(
                allowed_writes=abspaths({"foo.d"}),
                allowed_reads=abspaths({"foo.d"})))


class DiagnoseStaleOutputsTest(unittest.TestCase):

    def test_no_accesses_no_constraints(self):
        output_diagnostics = action_tracer.diagnose_stale_outputs(
            accesses=[],
            access_constraints=action_tracer.AccessConstraints(),
        )
        self.assertEqual(
            output_diagnostics,
            action_tracer.StalenessDiagnostics(),
        )

    def test_missing_write_no_inputs(self):
        required_writes = {"write.me"}
        output_diagnostics = action_tracer.diagnose_stale_outputs(
            accesses=[],
            access_constraints=action_tracer.AccessConstraints(
                required_writes=required_writes),
        )
        self.assertEqual(
            output_diagnostics,
            action_tracer.StalenessDiagnostics(
                required_writes=required_writes,
                nonexistent_outputs={"write.me"}),
        )

    def test_missing_write_with_used_input(self):
        used_input = "read.me"
        required_writes = {"write.me"}
        output_diagnostics = action_tracer.diagnose_stale_outputs(
            accesses=[action_tracer.Read(used_input)],
            access_constraints=action_tracer.AccessConstraints(
                allowed_reads={used_input},
                required_writes=required_writes,
            ),
        )
        self.assertEqual(
            output_diagnostics,
            action_tracer.StalenessDiagnostics(
                required_writes=required_writes,
                nonexistent_outputs={"write.me"}),
        )

    def test_stale_output_no_inputs(self):
        required_writes = {"write.me"}
        with mock.patch.object(os.path, 'exists',
                               return_value=True) as mock_exists:
            output_diagnostics = action_tracer.diagnose_stale_outputs(
                accesses=[],
                access_constraints=action_tracer.AccessConstraints(
                    required_writes=required_writes),
            )
        mock_exists.assert_called_once()
        self.assertEqual(
            output_diagnostics,
            action_tracer.StalenessDiagnostics(required_writes=required_writes),
        )

    def test_stale_output_with_used_input(self):

        def fake_read_ctime(path: str):
            if path.startswith("read"):
                return 200
            raise ValueError(f'Unexpected path: {path}')

        def fake_write_ctime(path: str):
            if path.startswith("write"):
                return 100
            raise ValueError(f'Unexpected path: {path}')

        used_input = "read.me"
        required_writes = {"write.me"}
        with mock.patch.object(os.path, 'exists',
                               return_value=True) as mock_exists:
            with mock.patch.object(os.path, 'getctime',
                                   wraps=fake_read_ctime) as mock_read_ctime:
                with mock.patch.object(
                        action_tracer, 'realpath_ctime',
                        wraps=fake_write_ctime) as mock_write_ctime:
                    output_diagnostics = action_tracer.diagnose_stale_outputs(
                        accesses=[action_tracer.Read(used_input)],
                        access_constraints=action_tracer.AccessConstraints(
                            allowed_reads={used_input},
                            required_writes=required_writes),
                    )
        mock_exists.assert_called_once()
        mock_read_ctime.assert_called()
        mock_write_ctime.assert_called()
        self.assertEqual(
            output_diagnostics,
            action_tracer.StalenessDiagnostics(
                required_writes=required_writes,
                newest_input=used_input,
                stale_outputs={"write.me"}),
        )

    def test_stale_output_with_multiple_used_inputs(self):

        def fake_read_ctime(path: str):
            if path == "read.me":
                return 200
            if path == "read.me.newer":
                return 300
            raise Exception(f'fake_read_ctime for unexpected path: {path}')

        def fake_write_ctime(path: str):
            if path.startswith("write"):
                return 250
            raise Exception(f'fake_write_ctime for unexpected path: {path}')

        used_input = "read.me"
        # Make sure the timestamp of the newest input is used for comparison.
        used_input_newer = "read.me.newer"
        required_writes = {"write.me"}
        with mock.patch.object(os.path, 'exists',
                               return_value=True) as mock_exists:
            with mock.patch.object(os.path, 'getctime',
                                   wraps=fake_read_ctime) as mock_read_ctime:
                with mock.patch.object(
                        action_tracer, 'realpath_ctime',
                        wraps=fake_write_ctime) as mock_write_ctime:
                    output_diagnostics = action_tracer.diagnose_stale_outputs(
                        accesses=[
                            action_tracer.Read(used_input),
                            action_tracer.Read(used_input_newer),
                        ],
                        access_constraints=action_tracer.AccessConstraints(
                            allowed_reads={used_input, used_input_newer},
                            required_writes=required_writes),
                    )
        mock_exists.assert_called_once()
        mock_read_ctime.assert_called()
        mock_write_ctime.assert_called()
        self.assertEqual(
            output_diagnostics,
            action_tracer.StalenessDiagnostics(
                required_writes=required_writes,
                # newer input is used for comparison
                newest_input=used_input_newer,
                stale_outputs={"write.me"}),
        )

    def test_fresh_output_with_used_input(self):

        def fake_getctime(path: str):
            if path.startswith("read"):
                return 100
            if path.startswith("write"):
                return 200
            return 0

        used_input = "read.me"
        written_output = "write.me"
        with mock.patch.object(os.path, 'exists',
                               return_value=True) as mock_exists:
            with mock.patch.object(os.path, 'getctime',
                                   wraps=fake_getctime) as mock_ctime:
                output_diagnostics = action_tracer.diagnose_stale_outputs(
                    accesses=[
                        action_tracer.Read(used_input),
                        action_tracer.Write(written_output),
                    ],
                    access_constraints=action_tracer.AccessConstraints(
                        allowed_reads={used_input},
                        required_writes={written_output}),
                )
        # There are no untouched outputs, so getctime is never called.
        mock_exists.assert_not_called()
        mock_ctime.assert_not_called()
        self.assertEqual(
            output_diagnostics,
            action_tracer.StalenessDiagnostics(
                required_writes={written_output},
                # newest_input is not evaluated
                stale_outputs=set(),
            ),
        )

    def test_fresh_output_with_used_input_readable_output(self):

        def fake_getctime(path: str):
            if path.startswith("read"):
                return 100
            if path.startswith("write"):
                return 200
            return 0

        used_input = "read.me"
        written_output = "write.me"
        with mock.patch.object(os.path, 'exists',
                               return_value=True) as mock_exists:
            with mock.patch.object(os.path, 'getctime',
                                   wraps=fake_getctime) as mock_ctime:
                output_diagnostics = action_tracer.diagnose_stale_outputs(
                    accesses=[
                        action_tracer.Read(used_input),
                        action_tracer.Read(written_output),
                        action_tracer.Write(written_output),
                    ],
                    access_constraints=action_tracer.AccessConstraints(
                        allowed_reads={used_input, written_output},
                        required_writes={written_output}),
                )
        # There are no untouched outputs, so getctime is never called.
        mock_exists.assert_not_called()
        mock_ctime.assert_not_called()
        self.assertEqual(
            output_diagnostics,
            action_tracer.StalenessDiagnostics(
                required_writes={written_output},
                # newest_input is not evaluated
                stale_outputs=set(),
            ),
        )


class MainArgParserTests(unittest.TestCase):

    # These args are required, and there's nothing interesting about them to test.
    required_args = "--trace-output t.out --label //pkg:tgt "

    def test_only_required_args(self):
        parser = action_tracer.main_arg_parser()
        args = parser.parse_args(self.required_args.split())
        self.assertEqual(args.trace_output, "t.out")
        self.assertEqual(args.label, "//pkg:tgt")
        # Make sure some checks are enabled by default
        self.assertTrue(args.check_access_permissions)

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


if __name__ == '__main__':
    unittest.main()
