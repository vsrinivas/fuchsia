#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for verify_sdk_compatibility.py"""

import json
import os
import tarfile
import tempfile
import unittest
from tempfile import TemporaryDirectory
from verify_sdk_compatibility import (
    fail_on_changes, fail_on_breaking_changes, GoldenLayoutMismatchError,
    SdkCompatibilityError, MissingInputError, generate_sdk_layout_golden_file)


class VerifySdkCompatibilityTests(unittest.TestCase):

    def test_fail_on_changes(self):
        with TemporaryDirectory() as root_build_dir:
            golden = os.path.join(root_build_dir, "test_file")
            start = "/"
            manifest = {
                "paths":
                    [{
                        "name": os.path.relpath(golden, start),
                        "type": "file"
                    }]
            }
            with open(golden, 'w') as f:
                json.dump(manifest, f)

            temp1 = os.path.join(root_build_dir, "test_file2")
            manifest2 = {
                "paths":
                    [
                        {
                            "name": os.path.relpath(golden, start),
                            "type": "file"
                        }, {
                            "name": os.path.relpath(temp1, start),
                            "type": "file"
                        }
                    ]
            }
            with open(temp1, 'w') as f:
                json.dump(manifest2, f)
            temp2 = os.path.join(root_build_dir, "test_file3")
            with open(temp2, 'w') as f:
                json.dump("test", f)

            b_golden = os.path.join(root_build_dir, "test_file4")
            manifest3 = {
                "paths":
                    [
                        {
                            "name": os.path.relpath(golden, start),
                            "type": "directory"
                        }
                    ]
            }
            with open(b_golden, 'w') as f:
                json.dump(manifest3, f)

            with tempfile.NamedTemporaryFile('wb', suffix='.tar.gz',
                                             delete=False) as f2:
                with tarfile.open(f2.name, mode='w:gz') as tar_curr:
                    tar_curr.add(golden)
                # Assert that a normal case should pass.
                self.assertTrue(fail_on_changes(f2.name, golden, 0) is None)
                # Assert that a mismatch of types will not pass.
                with self.assertRaises(GoldenLayoutMismatchError):
                    fail_on_changes(f2.name, b_golden, 0)
                # Assert that a nonexistent golden file will not pass.
                mock_gold = "path/to/core.golden"
                with self.assertRaises(MissingInputError):
                    fail_on_changes(f2.name, mock_gold, 0)
                # Assert that files with different ordfer, same contents will pass.
                with tarfile.open(f2.name, mode='w:gz') as tar_curr:
                    tar_curr.add(temp1)
                    tar_curr.add(golden)
                self.assertTrue(fail_on_changes(f2.name, temp1, 0) is None)
                # Assert that files with different contents will not pass.
                with tarfile.open(f2.name, mode='w:gz') as tar_curr:
                    tar_curr.add(temp1)
                    tar_curr.add(temp2)
                with self.assertRaises(GoldenLayoutMismatchError):
                    fail_on_changes(f2.name, temp1, 0)
                # Assert that lists of a different size will not pass.
                with tarfile.open(f2.name, mode='w:gz') as tar_curr:
                    tar_curr.add(temp1)
                with self.assertRaises(GoldenLayoutMismatchError):
                    fail_on_changes(f2.name, temp1, 0)
                # Assert that lists of a different size will not pass.
                with tarfile.open(f2.name, mode='w:gz') as tar_curr:
                    tar_curr.add(temp1)
                    tar_curr.add(temp1)
                    tar_curr.add(golden)
                with self.assertRaises(GoldenLayoutMismatchError):
                    fail_on_changes(f2.name, temp1, 0)

            with tempfile.NamedTemporaryFile('wb', suffix='.tar.gz',
                                             delete=False) as f3:
                # Assert that an empty tar file will not pass.
                with self.assertRaises(MissingInputError):
                    fail_on_changes(f3.name, golden, 0)

    def test_fail_on_breaking_changes(self):
        with TemporaryDirectory() as root_build_dir:
            golden = os.path.join(root_build_dir, "test_file")
            start = "/"
            manifest = {
                "paths":
                    [{
                        "name": os.path.relpath(golden, start),
                        "type": "file"
                    }]
            }
            with open(golden, 'w') as f:
                json.dump(manifest, f)

            b_golden = os.path.join(root_build_dir, "test_file4")
            manifest3 = {
                "paths":
                    [
                        {
                            "name": os.path.relpath(golden, start),
                            "type": "directory"
                        }
                    ]
            }
            with open(b_golden, 'w') as f:
                json.dump(manifest3, f)

            temp1 = os.path.join(root_build_dir, "test_file2")
            with open(temp1, 'w') as fil:
                json.dump(manifest, fil)

            temp2 = os.path.join(root_build_dir, "test_file3")
            manifest2 = {
                "paths":
                    [
                        {
                            "name": os.path.relpath(golden, start),
                            "type": "file"
                        }, {
                            "name": os.path.relpath(temp2, start),
                            "type": "file"
                        }
                    ]
            }
            with open(temp2, 'w') as f:
                json.dump(manifest2, f)

            with tempfile.NamedTemporaryFile('wb', suffix='.tar.gz',
                                             delete=False) as f:
                with tarfile.open(f.name, mode='w:gz') as tar_curr:
                    tar_curr.add(golden)
                # Assert that a case with no changes should pass.
                self.assertTrue(
                    fail_on_breaking_changes(f.name, golden, 0) is None)
                # Assert that a mismatch of types will not pass.
                with self.assertRaises(SdkCompatibilityError):
                    fail_on_breaking_changes(f.name, b_golden, 0)
                # Assert that a nonexistent golden file will not pass.
                mock_gold = "path/to/core.golden"
                with self.assertRaises(MissingInputError):
                    fail_on_breaking_changes(f.name, mock_gold, 0)
                # Assert that files with different order, same contents will pass.
                with tarfile.open(f.name, mode='w:gz') as tar_curr:
                    tar_curr.add(temp2)
                    tar_curr.add(golden)
                self.assertTrue(
                    fail_on_breaking_changes(f.name, temp2, 0) is None)

                # Assert that a case with non-breaking changes should pass (addition).
                # Output makes the developer acknowledge a stale golden file.
                with tarfile.open(f.name, mode='w:gz') as tar_curr:
                    tar_curr.add(golden)
                    tar_curr.add(temp1)
                with self.assertRaises(GoldenLayoutMismatchError):
                    fail_on_breaking_changes(f.name, golden, 0)
                # Assert that a case with breaking changes will not pass (deletion).
                with tarfile.open(f.name, mode='w:gz') as tar_curr:
                    tar_curr.add(temp2)
                with self.assertRaises(SdkCompatibilityError):
                    fail_on_breaking_changes(f.name, temp2, 0)

            with tempfile.NamedTemporaryFile('wb', suffix='.tar.gz',
                                             delete=False) as f3:
                # Assert that an empty tar file will not pass.
                with self.assertRaises(MissingInputError):
                    fail_on_breaking_changes(f3.name, golden, 0)

    def test_generate_sdk_layout_golden_file(self):
        with TemporaryDirectory() as root_build_dir:
            file1 = os.path.join(root_build_dir, "test_file1")
            with open(file1, 'w') as f:
                f.write("file_content")

            file2 = os.path.join(root_build_dir, "test_file2")
            with open(file2, 'w') as f:
                f.write("file_content")

            start = "/"
            json_layout = {
                "paths":
                    [
                        {
                            "name": os.path.relpath(file1, start),
                            "type": "file"
                        }, {
                            "name": os.path.relpath(file2, start),
                            "type": "file"
                        }
                    ]
            }

            with tempfile.NamedTemporaryFile('wb', suffix='.tar.gz',
                                             delete=False) as f:
                with tarfile.open(f.name, mode='w:gz') as tar_curr:
                    tar_curr.add(file1)
                    tar_curr.add(file2)
                self.assertTrue(
                    str(generate_sdk_layout_golden_file(f.name)) == str(
                        json_layout))


if __name__ == '__main__':
    unittest.main()
