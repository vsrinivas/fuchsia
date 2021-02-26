#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import subprocess
import unittest
from unittest import mock

import output_cacher


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


class ReplaceOutputArgsTest(unittest.TestCase):

    def test_command_failed(self):
        action = output_cacher.Action(command=["run.sh"], outputs={})
        with mock.patch.object(subprocess, "call", return_value=1) as mock_call:
            with mock.patch.object(output_cacher,
                                   "move_if_different") as mock_update:
                self.assertEqual(action.run_cached(".tmp"), 1)
        mock_call.assert_called()
        mock_update.assert_not_called()


if __name__ == '__main__':
    unittest.main()
