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
                    core_limit=21,
                    core_creep_limit=22,
                    distributed_shlibs=["lib_a", "lib_b"],
                    distributed_shlibs_limit=31,
                    distributed_shlibs_creep_limit=32,
                    icu_data=["icu"],
                    icu_data_limit=41,
                    icu_data_creep_limit=42,
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
                        dict(
                            component="/system (drivers and early boot)",
                            creep_limit=52,
                            limit=53,
                            src=[])
                    ],
                ),
                image_assembly_config=dict(
                    base=[
                        "obj/a/b1/c1/package_manifest.json",
                        "obj/a/b/c2/package_manifest.json",
                        "obj/is_not_matched_1/package_manifest.json",
                    ],
                    cache=[
                        "obj/a/b/c1/package_manifest.json",
                        "obj/a/b2/c1/package_manifest.json",
                        "obj/a/b1/c2/package_manifest.json",
                        "obj/is_not_matched_2/package_manifest.json",
                    ],
                    system=["obj/system/package_manifest.json"],
                ),
                expected_output=dict(
                    resource_budgets=[
                        dict(
                            name="Distributed shared libraries",
                            paths=["lib_a", "lib_b"],
                            budget_bytes=31,
                            creep_budget_bytes=32),
                        dict(
                            name="ICU Data",
                            paths=["icu"],
                            budget_bytes=41,
                            creep_budget_bytes=42)
                    ],
                    package_set_budgets=[
                        dict(
                            name="/system (drivers and early boot)",
                            budget_bytes=53,
                            creep_budget_bytes=52,
                            merge=True,
                            packages=["obj/system/package_manifest.json"]),
                        dict(
                            name="Core system+services",
                            budget_bytes=21,
                            creep_budget_bytes=22,
                            merge=False,
                            packages=[
                                "obj/is_not_matched_1/package_manifest.json",
                                "obj/is_not_matched_2/package_manifest.json",
                            ]),
                        dict(
                            name='b1_2_comp',
                            budget_bytes=20,
                            creep_budget_bytes=12,
                            merge=False,
                            packages=[
                                'obj/a/b1/c1/package_manifest.json',
                                'obj/a/b1/c2/package_manifest.json',
                                'obj/a/b2/c1/package_manifest.json',
                            ]),
                        dict(
                            name='b_comp',
                            budget_bytes=22,
                            creep_budget_bytes=16,
                            merge=False,
                            packages=[
                                'obj/a/b/c1/package_manifest.json',
                                'obj/a/b/c2/package_manifest.json',
                            ]),
                    ]),
                return_value=0),
            param(
                name="failure_when_manifest_is_matched_twice",
                size_limits=dict(
                    core_limit=21,
                    core_creep_limit=22,
                    distributed_shlibs=[],
                    distributed_shlibs_limit=31,
                    distributed_shlibs_creep_limit=32,
                    icu_data=[],
                    icu_data_limit=41,
                    icu_data_creep_limit=42,
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
                image_assembly_config=dict(
                    base=["obj/a/b/c2/package_manifest.json"]),
                expected_output=None,
                return_value=1),
            param(
                name=
                "success_when_size_limit_and_image_assembly_config_is_empty",
                size_limits=dict(),
                image_assembly_config=dict(),
                expected_output=dict(
                    resource_budgets=[], package_set_budgets=[]),
                return_value=0),
        ])
    def test_run_main(
            self, name, size_limits, image_assembly_config, expected_output,
            return_value):
        self.maxDiff = None
        with tempfile.TemporaryDirectory() as tmpdir:

            size_limits_path = os.path.join(tmpdir, "size_limits.json")
            with open(size_limits_path, "w") as file:
                json.dump(size_limits, file)

            image_assembly_config_path = os.path.join(
                tmpdir, "image_assembly_config.json")
            with open(image_assembly_config_path, "w") as file:
                json.dump(image_assembly_config, file)

            output_path = os.path.join(tmpdir, "output.json")
            # The first argument of a command line is the path to the program.
            # It is unused and left empty.
            sys.argv = [
                "", "--size_limits", size_limits_path,
                "--image_assembly_config", image_assembly_config_path,
                "--output", output_path
            ]

            self.assertEqual(convert_size_limits.main(), return_value)

            if expected_output is not None:
                with open(output_path, "r") as file:
                    self.assertEqual(expected_output, json.load(file))
