#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import subprocess
import unittest
from unittest import mock

import output_cacher


class TempFileTransformTests(unittest.TestCase):

    def test_invalid(self):
        self.assertFalse(output_cacher.TempFileTransform().valid)

    def test_temp_dir_only(self):
        self.assertTrue(
            output_cacher.TempFileTransform(temp_dir="my_tmp").valid)
        self.assertTrue(
            output_cacher.TempFileTransform(temp_dir="/my_tmp").valid)

    def test_basename_prefix_only(self):
        self.assertTrue(
            output_cacher.TempFileTransform(basename_prefix="temp-").valid)

    def test_suffix_only(self):
        self.assertTrue(output_cacher.TempFileTransform(suffix=".tmp").valid)

    def test_temp_dir_and_suffix(self):
        self.assertTrue(
            output_cacher.TempFileTransform(
                temp_dir="throw/away", suffix=".tmp").valid)

    def test_transform_suffix_only(self):
        self.assertEqual(
            output_cacher.TempFileTransform(
                suffix=".tmp").transform("foo/bar.o"), "foo/bar.o.tmp")

    def test_transform_temp_dir_only(self):
        self.assertEqual(
            output_cacher.TempFileTransform(
                temp_dir="t/m/p").transform("foo/bar.o"), "t/m/p/foo/bar.o")

    def test_transform_suffix_and_temp_dir(self):
        self.assertEqual(
            output_cacher.TempFileTransform(
                temp_dir="/t/m/p", suffix=".foo").transform("foo/bar.o"),
            "/t/m/p/foo/bar.o.foo")

    def test_transform_basename_prefix_only(self):
        self.assertEqual(
            output_cacher.TempFileTransform(
                basename_prefix="fake.").transform("bar.o"), "fake.bar.o")
        self.assertEqual(
            output_cacher.TempFileTransform(
                basename_prefix="fake.").transform("foo/bar.o"),
            "foo/fake.bar.o")

    def test_transform_basename_prefix_and_temp_dir(self):
        self.assertEqual(
            output_cacher.TempFileTransform(
                temp_dir="/t/m/p",
                basename_prefix="xyz-").transform("foo/bar.o"),
            "/t/m/p/foo/xyz-bar.o")


class SplitTransformJoinTest(unittest.TestCase):

    def test_no_change(self):
        self.assertEqual(
            output_cacher.split_transform_join('text', '=', lambda x: x),
            'text')

    def test_repeat(self):
        self.assertEqual(
            output_cacher.split_transform_join('text', '=', lambda x: x + x),
            'texttext')

    def test_with_split(self):
        self.assertEqual(
            output_cacher.split_transform_join('a=b', '=', lambda x: x + x),
            'aa=bb')

    def test_with_split_recorded(self):
        renamed_tokens = {}

        def recorded_transform(x):
            new_text = x + x
            renamed_tokens[x] = new_text
            return new_text

        self.assertEqual(
            output_cacher.split_transform_join('a=b', '=', recorded_transform),
            'aa=bb')
        self.assertEqual(renamed_tokens, {'a': 'aa', 'b': 'bb'})


class OutputSubstitutionTest(unittest.TestCase):

    def test_no_prev_opt(self):
        subst = output_cacher.OutputSubstitution('foo')
        self.assertEqual(subst.output_name, 'foo')
        self.assertEqual(subst.match_previous_option, '')

    def test_with_prev_opt(self):
        subst = output_cacher.OutputSubstitution('substitute_after:--out:foo')
        self.assertEqual(subst.output_name, 'foo')
        self.assertEqual(subst.match_previous_option, '--out')

    def test_malformed_prev_opt(self):
        with self.assertRaises(ValueError):
            output_cacher.OutputSubstitution('substitute_after:--outfoo')


class LexicallyRewriteTokenTest(unittest.TestCase):

    def test_repeat_text(self):
        self.assertEqual(
            output_cacher.lexically_rewrite_token('foo', lambda x: x + x),
            'foofoo')

    def test_delimters_only(self):
        self.assertEqual(
            output_cacher.lexically_rewrite_token(',,==,=,=,', lambda x: x + x),
            ',,==,=,=,')

    def test_flag_with_value(self):

        def transform(x):
            if x.startswith('file'):
                return 'tmp-' + x
            else:
                return x

        self.assertEqual(
            output_cacher.lexically_rewrite_token('--foo=file1', transform),
            '--foo=tmp-file1')
        self.assertEqual(
            output_cacher.lexically_rewrite_token(
                'notfile,file1,file2,notfile', transform),
            'notfile,tmp-file1,tmp-file2,notfile')
        self.assertEqual(
            output_cacher.lexically_rewrite_token(
                '--foo=file1,file2', transform), '--foo=tmp-file1,tmp-file2')


class MoveIfDifferentTests(unittest.TestCase):

    def test_nonexistent_source(self):
        with mock.patch.object(os.path, "exists",
                               return_value=False) as mock_exists:
            with mock.patch.object(shutil, "move") as mock_move:
                with mock.patch.object(os, "remove") as mock_remove:
                    output_cacher.move_if_different("source.txt", "dest.txt")
        mock_exists.assert_called()
        mock_move.assert_not_called()
        mock_remove.assert_not_called()

    def test_new_output(self):

        def fake_exists(path):
            if path == "source.txt":
                return True
            elif path == "dest.txt":
                return False

        with mock.patch.object(os.path, "exists",
                               wraps=fake_exists) as mock_exists:
            with mock.patch.object(output_cacher, "files_match") as mock_diff:
                with mock.patch.object(shutil, "move") as mock_move:
                    with mock.patch.object(os, "remove") as mock_remove:
                        output_cacher.move_if_different(
                            "source.txt", "dest.txt")
        mock_exists.assert_called()
        mock_diff.assert_not_called()
        mock_move.assert_called_with("source.txt", "dest.txt")
        mock_remove.assert_not_called()

    def test_updating_output(self):
        with mock.patch.object(os.path, "exists",
                               return_value=True) as mock_exists:
            with mock.patch.object(output_cacher, "files_match",
                                   return_value=False) as mock_diff:
                with mock.patch.object(shutil, "move") as mock_move:
                    with mock.patch.object(os, "remove") as mock_remove:
                        output_cacher.move_if_different(
                            "source.txt", "dest.txt")
        mock_exists.assert_called()
        mock_diff.assert_called()
        mock_move.assert_called_with("source.txt", "dest.txt")
        mock_remove.assert_not_called()

    def test_cached_output(self):
        with mock.patch.object(os.path, "exists",
                               return_value=True) as mock_exists:
            with mock.patch.object(output_cacher, "files_match",
                                   return_value=True) as mock_diff:
                with mock.patch.object(shutil, "move") as mock_move:
                    with mock.patch.object(os, "remove") as mock_remove:
                        output_cacher.move_if_different(
                            "source.txt", "dest.txt")
        mock_exists.assert_called()
        mock_diff.assert_called()
        mock_move.assert_not_called()
        mock_remove.assert_called_with("source.txt")


class SubstituteCommandTest(unittest.TestCase):

    def test_no_substitutions(self):
        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(command=["run.sh"], substitutions={})
        repl, renamed = action.substitute_command(transform)
        self.assertEqual(repl, ["run.sh"])
        self.assertEqual(renamed, {})

    def test_one_suffix(self):
        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(
            command=["run.sh", "out.txt"], substitutions={"out.txt": ""})
        repl, renamed = action.substitute_command(transform)
        self.assertEqual(repl, ["run.sh", "out.txt.tmp"])
        self.assertEqual(renamed, {"out.txt": "out.txt.tmp"})

    def test_require_flag_match_missing_flag(self):
        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(
            command=["run.sh", "out.txt"], substitutions={"out.txt": "-f"})
        repl, renamed = action.substitute_command(transform)
        self.assertEqual(repl, ["run.sh", "out.txt"])
        self.assertEqual(renamed, {})

    def test_require_flag_match_wrong_flag(self):
        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(
            command=["run.sh", "-g", "out.txt"],
            substitutions={"out.txt": "-f"})
        repl, renamed = action.substitute_command(transform)
        self.assertEqual(repl, ["run.sh", "-g", "out.txt"])
        self.assertEqual(renamed, {})

    def test_require_flag_match_have_flag(self):
        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(
            command=["run.sh", "-f", "out.txt"],
            substitutions={"out.txt": "-f"})
        repl, renamed = action.substitute_command(transform)
        self.assertEqual(repl, ["run.sh", "-f", "out.txt.tmp"])
        self.assertEqual(renamed, {"out.txt": "out.txt.tmp"})

    def test_match_only_one_flag(self):
        transform = output_cacher.TempFileTransform(basename_prefix="tmp-")
        action = output_cacher.Action(
            command=["run.sh", "-x", "out.txt", "-f", "out.txt"],
            substitutions={"out.txt": "-f"})
        repl, renamed = action.substitute_command(transform)
        self.assertEqual(repl, ["run.sh", "-x", "out.txt", "-f", "tmp-out.txt"])
        self.assertEqual(renamed, {"out.txt": "tmp-out.txt"})

    def test_match_only_one_flag_first_occurrence(self):
        transform = output_cacher.TempFileTransform(basename_prefix="tmp-")
        action = output_cacher.Action(
            command=["run.sh", "out.txt", "-f", "out.txt", "out.txt"],
            substitutions={"out.txt": "-f"})
        repl, renamed = action.substitute_command(transform)
        self.assertEqual(
            repl, ["run.sh", "out.txt", "-f", "tmp-out.txt", "out.txt"])
        self.assertEqual(renamed, {"out.txt": "tmp-out.txt"})


class ReplaceOutputArgsTest(unittest.TestCase):

    def test_dry_run(self):
        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(command=["run.sh"], substitutions={})
        with mock.patch.object(subprocess, "call") as mock_call:
            self.assertEqual(action.run_cached(transform, dry_run=True), 0)
        mock_call.assert_not_called()

    def test_command_failed(self):
        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(command=["run.sh"], substitutions={})
        with mock.patch.object(subprocess, "call", return_value=1) as mock_call:
            with mock.patch.object(output_cacher,
                                   "move_if_different") as mock_update:
                self.assertEqual(action.run_cached(transform), 1)
        mock_call.assert_called_with(["run.sh"])
        mock_update.assert_not_called()

    def test_command_passed_using_suffix(self):
        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(
            command=["run.sh", "in.put", "out.put"],
            substitutions={"out.put": ""})
        with mock.patch.object(subprocess, "call", return_value=0) as mock_call:
            with mock.patch.object(output_cacher,
                                   "move_if_different") as mock_update:
                self.assertEqual(action.run_cached(transform), 0)
        mock_call.assert_called_with(["run.sh", "in.put", "out.put.tmp"])
        mock_update.assert_called_with(
            src="out.put.tmp", dest="out.put", verbose=False)

    def test_command_passed_using_relative_temp_dir(self):
        transform = output_cacher.TempFileTransform(temp_dir="temp/temp")
        action = output_cacher.Action(
            command=["run.sh", "in.put", "foo/out.put"],
            substitutions={"foo/out.put": ""})
        with mock.patch.object(subprocess, "call", return_value=0) as mock_call:
            with mock.patch.object(output_cacher,
                                   "move_if_different") as mock_update:
                with mock.patch.object(os, "makedirs") as mock_mkdir:
                    self.assertEqual(action.run_cached(transform), 0)
        mock_mkdir.assert_called_with("temp/temp/foo", exist_ok=True)
        mock_call.assert_called_with(
            ["run.sh", "in.put", "temp/temp/foo/out.put"])
        mock_update.assert_called_with(
            src="temp/temp/foo/out.put", dest="foo/out.put", verbose=False)


class RunTwiceCompareTests(unittest.TestCase):

    def test_command_failed(self):
        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(command=["run.sh"], substitutions={})
        with mock.patch.object(subprocess, "call", return_value=1) as mock_call:
            with mock.patch.object(output_cacher, "files_match") as mock_match:
                with mock.patch.object(os.path, "exists",
                                       return_value=True) as mock_exists:
                    with mock.patch.object(shutil, "copy2") as mock_copy:
                        with mock.patch.object(os, "makedirs") as mock_mkdir:
                            self.assertEqual(
                                action.run_twice_and_compare_outputs(transform),
                                1)
        mock_call.assert_called_once_with(["run.sh"])
        mock_match.assert_not_called()
        mock_exists.assert_not_called()
        mock_mkdir.assert_not_called()
        mock_copy.assert_not_called()

    def test_command_passed_and_rerun_matches(self):
        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(
            command=["run.sh", "in.put", "out.put"],
            substitutions={"out.put": ""})
        with mock.patch.object(subprocess, "call", return_value=0) as mock_call:
            with mock.patch.object(output_cacher, "files_match",
                                   return_value=True) as mock_match:
                with mock.patch.object(os.path, "isfile",
                                       return_value=True) as mock_isfile:
                    with mock.patch.object(os, "remove") as mock_remove:
                        with mock.patch.object(os, "makedirs") as mock_mkdir:
                            with mock.patch.object(shutil,
                                                   "copy2") as mock_copy:
                                self.assertEqual(
                                    action.run_twice_and_compare_outputs(
                                        transform), 0)
        mock_call.assert_has_calls(
            [
                mock.call(["run.sh", "in.put", "out.put"]),
                mock.call(["run.sh", "in.put", "out.put"]),
            ],
            any_order=True)
        mock_match.assert_called_with("out.put", "out.put.tmp")
        mock_remove.assert_called_with("out.put.tmp")
        mock_isfile.assert_called()
        mock_mkdir.assert_not_called()  # using suffix, not temp_dir
        mock_copy.assert_called_once_with(
            "out.put", "out.put.tmp", follow_symlinks=False)

    def test_command_passed_and_rerun_differs(self):
        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(
            command=["run.sh", "in.put", "out.put"],
            substitutions={"out.put": ""})
        with mock.patch.object(subprocess, "call", return_value=0) as mock_call:
            with mock.patch.object(output_cacher, "files_match",
                                   return_value=False) as mock_match:
                with mock.patch.object(os.path, "isfile",
                                       return_value=True) as mock_isfile:
                    with mock.patch.object(os, "remove") as mock_remove:
                        with mock.patch.object(os, "makedirs") as mock_mkdir:
                            with mock.patch.object(shutil,
                                                   "copy2") as mock_copy:
                                self.assertEqual(
                                    action.run_twice_and_compare_outputs(
                                        transform), 1)
        mock_call.assert_has_calls(
            [
                mock.call(["run.sh", "in.put", "out.put"]),
                mock.call(["run.sh", "in.put", "out.put"]),
            ],
            any_order=True)
        mock_match.assert_called_with("out.put", "out.put.tmp")
        mock_remove.assert_not_called()
        mock_isfile.assert_called()
        mock_mkdir.assert_not_called()  # using suffix, not temp_dir
        mock_copy.assert_called_once_with(
            "out.put", "out.put.tmp", follow_symlinks=False)

    def test_command_passed_and_some_outptus_differ(self):

        def fake_match(file1: str, file2: str) -> bool:
            if file1 == "out.put":
                return True
            elif file1 == "out2.put":
                return False
            raise ValueError(f"Unhandled file name: {file1}")

        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(
            command=["run.sh", "in.put", "out.put", "out2.put"],
            substitutions={
                "out.put": "",
                "out2.put": ""
            })
        with mock.patch.object(subprocess, "call", return_value=0) as mock_call:
            with mock.patch.object(output_cacher, "files_match",
                                   wraps=fake_match) as mock_match:
                with mock.patch.object(os.path, "isfile",
                                       return_value=True) as mock_isfile:
                    with mock.patch.object(os, "remove") as mock_remove:
                        with mock.patch.object(os, "makedirs") as mock_mkdir:
                            with mock.patch.object(shutil,
                                                   "copy2") as mock_copy:
                                self.assertEqual(
                                    action.run_twice_and_compare_outputs(
                                        transform), 1)
        mock_call.assert_has_calls(
            [
                mock.call(["run.sh", "in.put", "out.put", "out2.put"]),
                mock.call(["run.sh", "in.put", "out.put", "out2.put"]),
            ],
            any_order=True)
        mock_match.assert_has_calls(
            [
                mock.call("out.put", "out.put.tmp"),
                mock.call("out2.put", "out2.put.tmp"),
            ],
            any_order=True)
        mock_remove.assert_has_calls([mock.call("out.put.tmp")])
        mock_isfile.assert_called()
        mock_mkdir.assert_not_called()  # using suffix, not temp_dir
        mock_copy.assert_has_calls(
            [
                mock.call("out.put", "out.put.tmp", follow_symlinks=False),
                mock.call("out2.put", "out2.put.tmp", follow_symlinks=False),
            ],
            any_order=True)


class RunTwiceWithSubstitutionCompareTests(unittest.TestCase):

    def test_command_failed(self):
        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(command=["run.sh"], substitutions={})
        with mock.patch.object(subprocess, "call", return_value=1) as mock_call:
            with mock.patch.object(output_cacher, "files_match") as mock_match:
                self.assertEqual(
                    action.run_twice_with_substitution_and_compare_outputs(
                        transform), 1)
        mock_call.assert_called_once_with(["run.sh"])
        mock_match.assert_not_called()

    def test_command_passed_and_rerun_matches(self):
        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(
            command=["run.sh", "in.put", "out.put"],
            substitutions={"out.put": ""})
        with mock.patch.object(subprocess, "call", return_value=0) as mock_call:
            with mock.patch.object(output_cacher, "files_match",
                                   return_value=True) as mock_match:
                with mock.patch.object(os, "remove") as mock_remove:
                    self.assertEqual(
                        action.run_twice_with_substitution_and_compare_outputs(
                            transform), 0)
        mock_call.assert_has_calls(
            [
                mock.call(["run.sh", "in.put", "out.put"]),
                mock.call(["run.sh", "in.put", "out.put.tmp"]),
            ],
            any_order=True)
        mock_match.assert_called_with("out.put", "out.put.tmp")
        mock_remove.assert_called_with("out.put.tmp")

    def test_command_passed_and_rerun_differs(self):
        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(
            command=["run.sh", "in.put", "out.put"],
            substitutions={"out.put": ""})
        with mock.patch.object(subprocess, "call", return_value=0) as mock_call:
            with mock.patch.object(output_cacher, "files_match",
                                   return_value=False) as mock_match:
                with mock.patch.object(os, "remove") as mock_remove:
                    self.assertEqual(
                        action.run_twice_with_substitution_and_compare_outputs(
                            transform), 1)
        mock_call.assert_has_calls(
            [
                mock.call(["run.sh", "in.put", "out.put"]),
                mock.call(["run.sh", "in.put", "out.put.tmp"]),
            ],
            any_order=True)
        mock_match.assert_called_with("out.put", "out.put.tmp")
        mock_remove.assert_not_called()

    def test_command_passed_and_some_outptus_differ(self):

        def fake_match(file1: str, file2: str) -> bool:
            if file1 == "out.put":
                return True
            elif file1 == "out2.put":
                return False
            raise ValueError(f"Unhandled file name: {file1}")

        transform = output_cacher.TempFileTransform(suffix=".tmp")
        action = output_cacher.Action(
            command=["run.sh", "in.put", "out.put", "out2.put"],
            substitutions={
                "out.put": "",
                "out2.put": ""
            })
        with mock.patch.object(subprocess, "call", return_value=0) as mock_call:
            with mock.patch.object(output_cacher, "files_match",
                                   wraps=fake_match) as mock_match:
                with mock.patch.object(os, "remove") as mock_remove:
                    self.assertEqual(
                        action.run_twice_with_substitution_and_compare_outputs(
                            transform), 1)
        mock_call.assert_has_calls(
            [
                mock.call(["run.sh", "in.put", "out.put", "out2.put"]),
                mock.call(["run.sh", "in.put", "out.put.tmp", "out2.put.tmp"]),
            ],
            any_order=True)
        mock_match.assert_has_calls(
            [
                mock.call("out.put", "out.put.tmp"),
                mock.call("out2.put", "out2.put.tmp"),
            ],
            any_order=True)
        mock_remove.assert_has_calls([mock.call("out.put.tmp")])


if __name__ == '__main__':
    unittest.main()
