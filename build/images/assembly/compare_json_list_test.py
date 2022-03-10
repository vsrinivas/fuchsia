#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
import tempfile
import compare_json_list
import os
import sys
from parameterized import parameterized, param


class CompareJsonListTest(unittest.TestCase):

    @parameterized.expand(
        [
            param(
                exit_code=0,
                key="nested-key",
                prefixes=["prefix1", "prefix2"],
                reference="""{
                    "nested-key": [
                        {
                            "somekey": "prefix1/somevalue",
                            "someotherkey": [
                                "prefix1/someothervalue"
                            ]
                        },
                        {
                            "somekey": "prefix2/somevalue"
                        }
                    ]
                }""",
                comparison="""{
                    "nested-key": [
                        {
                            "somekey": "prefix2/somevalue",
                            "someotherkey": [
                                "prefix2/someothervalue"
                            ]
                        },
                        {
                            "somekey": "prefix2/somevalue"
                        }
                    ]
                }""",
            ),
            param(
                exit_code=2,
                key="nested-key",
                prefixes=[],
                reference="""{
                    "nested-key": [
                        {
                            "somekey": "prefix1/somevalue",
                            "someotherkey": [
                                "prefix1/someothervalue"
                            ]
                        },
                        {
                            "somekey": "prefix2/somevalue"
                        }
                    ]
                }""",
                comparison="""{
                    "nested-key": [
                        {
                            "somekey": "prefix2/somevalue",
                            "someotherkey": [
                                "prefix2/someothervalue"
                            ]
                        },
                        {
                            "somekey": "prefix2/somevalue"
                        }
                    ]
                }""",
            ),
        ])
    def test_run_main(self, exit_code, key, prefixes, reference, comparison):
        with tempfile.TemporaryDirectory() as tmpdir:
            stamp_path = os.path.join(tmpdir, "stamp")
            reference_path = os.path.join(tmpdir, "reference.json")
            with open(reference_path, "w") as file:
                file.write(reference)
            comparison_path = os.path.join(tmpdir, "comparison.json")
            with open(comparison_path, "w") as file:
                file.write(comparison)
            sys.argv = [
                "", "--reference", reference_path, "--comparison",
                comparison_path, "--stamp", stamp_path
            ]
            sys.argv.extend(["--strip-prefix"] + prefixes)
            sys.argv.extend(["--list-key", key])

            result = compare_json_list.main()
            self.assertEqual(exit_code, result)

    def test_make_hashable(self):
        prefixes = []
        one = compare_json_list.make_hashable([1, 2, 3], prefixes)
        two = compare_json_list.make_hashable([3, 2, 1], prefixes)
        self.assertEqual(1, len(set([one, two])))

        one = compare_json_list.make_hashable({"one": 1, "two": 2}, prefixes)
        two = compare_json_list.make_hashable({"two": 2, "one": 1}, prefixes)
        self.assertEqual(1, len(set([one, two])))

        one = compare_json_list.make_hashable({"one": 2, "two": 1}, prefixes)
        two = compare_json_list.make_hashable({"one": 1, "two": 2}, prefixes)
        self.assertEqual(2, len(set([one, two])))

        one = compare_json_list.make_hashable({"key": [1, 2]}, prefixes)
        two = compare_json_list.make_hashable({"key": [2, 1]}, prefixes)
        self.assertEqual(1, len(set([one, two])))

        prefixes = ["prefix1", "prefix2"]
        one = compare_json_list.make_hashable(
            {
                "one": "prefix1/1",
                "two": 2
            }, prefixes)
        two = compare_json_list.make_hashable(
            {
                "one": "prefix2/1",
                "two": 2
            }, prefixes)
        self.assertEqual(1, len(set([one, two])))
