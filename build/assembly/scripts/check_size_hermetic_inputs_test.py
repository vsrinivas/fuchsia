# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
import tempfile
import check_size_hermetic_inputs
import os
import sys
from parameterized import parameterized, param


class ConvertTest(unittest.TestCase):

    @parameterized.expand(
        [
            param(
                budgets="""{
                    "package_set_budgets": [
                        {
                            "name": "/system (drivers and early boot)",
                            "budget_bytes": 3355444,
                            "packages": [
                                "obj/src/sys/pkg/bin/pkgfs/pkgfs/package_manifest.json",
                                "obj/src/sys/pkg/bin/pkgctl/pkgctl-bin/package_manifest.json",
                                "obj/src/sys/pkg/bin/system-update-committer/system-update-committer/package_manifest.json"
                            ]
                        },
                        {
                            "name": "Software Delivery",
                            "budget_bytes": 7497932,
                            "packages": [
                                "obj/src/sys/pkg/bin/pkgctl/pkgctl-bin/package_manifest.json",
                                "obj/src/sys/pkg/bin/update/update-bin/package_manifest.json"
                            ]
                        }
                    ]
                }""",
                expected_output=[
                    'obj/src/sys/pkg/bin/pkgctl/pkgctl-bin/package_manifest.json',
                    'obj/src/sys/pkg/bin/system-update-committer/system-update-committer/package_manifest.json',
                    'obj/src/sys/pkg/bin/pkgfs/pkgfs/package_manifest.json',
                    'obj/src/sys/pkg/bin/update/update-bin/package_manifest.json',
                ])
        ])
    def test_run_main(self, budgets, expected_output):
        with tempfile.TemporaryDirectory() as tmpdir:
            input_path = os.path.join(tmpdir, "budgets.json")
            with open(input_path, "w") as file:
                file.write(budgets)
            output_path = os.path.join(tmpdir, "output.d")
            sys.argv = ["", "--budgets", input_path, "--output", output_path]

            check_size_hermetic_inputs.main()

            actual_output = None
            with open(output_path, "r") as file:
                actual_output = file.read().strip().replace(
                    os.path.relpath(tmpdir), "{working_dir}").split("\n")
            self.assertEqual(sorted(expected_output), sorted(actual_output))
