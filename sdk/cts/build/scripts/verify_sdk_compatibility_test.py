#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for verify_sdk_compatibility.py"""

import json
import os
import tarfile
import tempfile
import re
import unittest
from tempfile import TemporaryDirectory
from verify_sdk_compatibility import (
    fail_on_breaking_changes, NotifyOnAdditions, SdkCompatibilityError,
    MissingInputError, generate_sdk_layout_golden_file)


class VerifySdkCompatibilityTests(unittest.TestCase):

    def test_fail_on_breaking_changes(self):
        with TemporaryDirectory() as root_build_dir:
            golden1 = os.path.join(root_build_dir, "test_gold1")
            afile = os.path.join(root_build_dir, "afile")
            with open(afile, 'w') as f:
                f.write("file_content")
            start = "/"
            manifest = {
                os.path.relpath(afile, start),
            }
            with open(golden1, 'w') as f:
                for p in manifest:
                    f.write(p)
                    f.write("\n")

            golden2 = os.path.join(root_build_dir, "test_gold2")
            manifest = {
                os.path.relpath(afile, start),
            }
            with open(golden2, 'w') as f:
                for p in manifest:
                    f.write(p + "/")
                    f.write("\n")

            bfile = os.path.join(root_build_dir, "bfile")
            with open(bfile, 'w') as f:
                f.write("file_content")
            golden3 = os.path.join(root_build_dir, "test_gold3")
            manifest = {
                os.path.relpath(afile, start),
                os.path.relpath(bfile, start)
            }
            with open(golden3, 'w') as f:
                for p in manifest:
                    f.write(p)
                    f.write("\n")
            cfile = os.path.join(root_build_dir, "cfile")
            with open(cfile, 'w') as f:
                f.write("file_content")
            dfile = os.path.join(root_build_dir, "dfile")
            with open(dfile, 'w') as f:
                f.write("file_content")
            efile = os.path.join(root_build_dir, "efile")
            with open(efile, 'w') as f:
                f.write("file_content")
            with tempfile.NamedTemporaryFile('wb', suffix='.tar.gz',
                                             delete=False) as f:
                with tarfile.open(f.name, mode='w:gz') as tar_curr:
                    tar_curr.add(afile)
                # Assert that a case with no changes should pass.
                self.assertTrue(
                    fail_on_breaking_changes(f.name, golden1) is None)
                # Assert that additions to the SDK layout will trigger a notification.
                with tarfile.open(f.name, mode='w:gz') as tar_curr:
                    tar_curr.add(afile)
                    tar_curr.add(bfile)
                    tar_curr.add(cfile)
                self.assertRaisesRegex(
                    NotifyOnAdditions, ".*(?=.*bfile)(?=.*cfile).*",
                    fail_on_breaking_changes, f.name, golden1)
                # Assert that a mismatch of types will not pass.
                with self.assertRaises(SdkCompatibilityError):
                    fail_on_breaking_changes(f.name, golden2)
                # Assert that a nonexistent golden file will not pass.
                mock_gold = "path/to/core.golden"
                with self.assertRaises(MissingInputError):
                    fail_on_breaking_changes(f.name, mock_gold)
                # Assert that files with different order, same contents will pass.
                with tarfile.open(f.name, mode='w:gz') as tar_curr:
                    tar_curr.add(bfile)
                    tar_curr.add(afile)
                self.assertTrue(
                    fail_on_breaking_changes(f.name, golden3) is None)
                # Assert that a case with breaking changes will not pass (deletion).
                with tarfile.open(f.name, mode='w:gz') as tar_curr:
                    tar_curr.add(bfile)
                self.assertRaisesRegex(
                    SdkCompatibilityError, ".*afile.*",
                    fail_on_breaking_changes, f.name, golden3)
                # Assert that a case with no matching files does not pass.
                with tarfile.open(f.name, mode='w:gz') as tar_curr:
                    tar_curr.add(dfile)
                    tar_curr.add(efile)
                    tar_curr.add(cfile)
                self.assertRaisesRegex(
                    SdkCompatibilityError, ".*(?=.*bfile)(?=.*afile).*",
                    fail_on_breaking_changes, f.name, golden3)
                # Assert that a case with one missing path does not pass.
                manifest = {
                    os.path.relpath(cfile, start),
                    os.path.relpath(bfile, start)
                }
                with open(golden3, 'w') as file:
                    for p in manifest:
                        file.write(p)
                        file.write("\n")
                self.assertRaisesRegex(
                    SdkCompatibilityError, ".*bfile*", fail_on_breaking_changes,
                    f.name, golden3)

            with tempfile.NamedTemporaryFile('wb', suffix='.tar.gz',
                                             delete=False) as f3:
                # Assert that an empty tar file will not pass.
                with self.assertRaises(MissingInputError):
                    fail_on_breaking_changes(f3.name, golden1)

    def test_generate_sdk_layout_golden_file(self):
        with TemporaryDirectory() as root_build_dir:
            file1 = os.path.join(root_build_dir, "test_file1")
            with open(file1, 'w') as f:
                f.write("file_content")

            file2 = os.path.join(root_build_dir, "test_file2")
            with open(file2, 'w') as f:
                f.write("file_content")

            start = "/"
            layout = {
                os.path.relpath(file1, start),
                os.path.relpath(file2, start)
            }

            with tempfile.NamedTemporaryFile('wb', suffix='.tar.gz',
                                             delete=False) as f:
                with tarfile.open(f.name, mode='w:gz') as tar_curr:
                    tar_curr.add(file1)
                    tar_curr.add(file2)
                self.assertTrue(
                    str(generate_sdk_layout_golden_file(f.name)) == str(layout))


if __name__ == '__main__':
    unittest.main()
