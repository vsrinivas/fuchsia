# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import unittest
import tempfile
import convert_size_limits
import os
import sys
import json
from parameterized import parameterized, param


class ConvertTest(unittest.TestCase):

    @parameterized.expand(
        [
            param(
                name="general_case",
                size_limits=dict(
                    components=[
                        dict(
                            component="b1_2_comp",
                            creep_limit=12,
                            limit=20,
                            src=["a/b1", "a/b2"]),
                        dict(
                            component="b_comp",
                            creep_limit=16,
                            limit=22,
                            src=["a/b"]),
                    ]),
                product_config=dict(
                    base=[
                        "obj/a/b1/c1/package_manifest.json",
                        "obj/a/b/c2/package_manifest.json"
                    ],
                    cache=[
                        "obj/a/b/c1/package_manifest.json",
                        "obj/a/b2/c1/package_manifest.json"
                    ],
                    system=["obj/a/b1/c2/package_manifest.json"],
                ),
                expected_output=[
                    dict(
                        name='b1_2_comp',
                        budget_bytes=20,
                        packages=[
                            'obj/a/b1/c1/package_manifest.json',
                            'obj/a/b2/c1/package_manifest.json',
                            'obj/a/b1/c2/package_manifest.json'
                        ]),
                    dict(
                        name='b_comp',
                        budget_bytes=22,
                        packages=[
                            'obj/a/b/c2/package_manifest.json',
                            'obj/a/b/c1/package_manifest.json'
                        ])
                ],
                return_value=0),
            param(
                name="failure_when_manifest_is_matched_twice",
                size_limits=dict(
                    components=[
                        dict(
                            component="comp1",
                            creep_limit=12,
                            limit=20,
                            src=["a"]),
                        dict(
                            component="comp2",
                            creep_limit=16,
                            limit=22,
                            src=["a/b"]),
                    ]),
                product_config=dict(base=["obj/a/b/c2/package_manifest.json"],
                                   ),
                expected_output=None,
                return_value=1),
            param(
                name="success_when_size_limit_is_empty",
                size_limits=dict(),
                product_config=dict(
                    base=[],
                    cache=[],
                    system=[],
                ),
                expected_output=[],
                return_value=0),
            param(
                name="success_when_there_is_no_base",
                size_limits=dict(components=[]),
                product_config=dict(
                    cache=[],
                    system=[],
                ),
                expected_output=[],
                return_value=0),
            param(
                name="success_when_there_is_no_cache",
                size_limits=dict(components=[]),
                product_config=dict(
                    base=[],
                    system=[],
                ),
                expected_output=[],
                return_value=0),
            param(
                name="success_when_there_is_no_system",
                size_limits=dict(components=[]),
                product_config=dict(
                    base=[],
                    cache=[],
                ),
                expected_output=[],
                return_value=0),
        ])
    def test_run_main(
            self, name, size_limits, product_config, expected_output,
            return_value):
        with tempfile.TemporaryDirectory() as tmpdir:

            size_limits_path = os.path.join(tmpdir, "size_limits.json")
            with open(size_limits_path, "w") as file:
                json.dump(size_limits, file)

            product_config_path = os.path.join(tmpdir, "product_config.json")
            with open(product_config_path, "w") as file:
                json.dump(product_config, file)

            output_path = os.path.join(tmpdir, "output.json")
            # The first argument of a command line is the path to the program.
            # It is unused and left empty.
            sys.argv = [
                "", "--size_limits", size_limits_path, "--product_config",
                product_config_path, "--output", output_path
            ]

            self.assertEqual(convert_size_limits.main(), return_value)

            if expected_output is not None:
                with open(output_path, "r") as file:
                    self.assertEqual(expected_output, json.load(file))
