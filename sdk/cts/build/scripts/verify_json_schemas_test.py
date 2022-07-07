#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Unit tests for verify_json_schemas.py"""
from tempfile import TemporaryDirectory
import json
import unittest
from verify_json_schemas import fail_on_changes


class VerifyJsonSchemaTests(unittest.TestCase):

    def generate_path(self, root_build_dir, file_name):
        return root_build_dir + "/" + file_name

    def test_fail_on_changes(self):
        data_A = {"some_key": "some value A"}
        data_B = {"some_key": "some value B"}
        manifest = {"atoms": []}

        with TemporaryDirectory() as root_build_dir:
            golden_m = self.generate_path(
                root_build_dir, "core.golden")
            manifest['atoms'].append(data_A)
            with open(golden_m, 'w') as f:
                json.dump(manifest, f)
            current_m0 = self.generate_path(root_build_dir, "core")
            with open(current_m0, 'w') as f:
                json.dump(manifest, f)
            # Assert that a normal case should pass
            self.assertTrue(fail_on_changes([current_m0], [golden_m]) is None)

            manifest['atoms'] = []

            current_m = self.generate_path(root_build_dir, "core")
            manifest['atoms'].append(data_B)
            with open(current_m, 'w') as f:
                json.dump(manifest, f)
            # Assert that files with different contents will not pass
            return_str = (
                f"Detected changes to JSON Schema {current_m}.\n"
                f"Please do not change the schemas without consulting with"
                f" sdk-dev@fuchsia.dev.\nTo prevent potential breaking SDK "
                f"integrators, the contents of {current_m} should match "
                f"{golden_m}.\nIf you have approval to make this change, "
                f"run: cp \"{current_m}\" \"{golden_m}\"\n")
            self.assertTrue(
                str(fail_on_changes([current_m], [golden_m])) == return_str)

            mock_gold = "path/to/core.golden"
            # Assert that a nonexistent golden file will not pass
            return_str = (
                f"Detected missing JSON Schema {mock_gold}.\n"
                f"Please consult with sdk-dev@fuchsia.dev if you are"
                f" planning to remove a schema.\n"
                f"If you have approval to make this change, remove the "
                f"schema and corresponding golden file from the schema lists.\n")
            self.assertTrue(
                str(fail_on_changes([current_m], [mock_gold])) == return_str)

            mock_curr = "path/to/core"
            # Assert that a nonexistent current file will not pass
            return_str = (
                f"Detected missing JSON Schema {mock_curr}.\n"
                f"Please consult with sdk-dev@fuchsia.dev if you are"
                f" planning to remove a schema.\n"
                f"If you have approval to make this change, remove the "
                f"schema and corresponding golden file from the schema lists.\n")
            self.assertTrue(
                str(fail_on_changes([mock_curr], [golden_m])) == return_str)

            # Assert that lists of a different size will not pass
            return_str = (
                f"Detected that the golden list contains "
                f"a different number of schemas than the current list.\n"
                f"Golden:\n['{golden_m}']\nCurrent:\n['{mock_curr}', '{golden_m}']\n"
                f"Please make sure each schema has a corresponding golden file,"
                f" and vice versa.\n")
            self.assertTrue(
                str(fail_on_changes([mock_curr, golden_m], [golden_m])) ==
                return_str)
            return_str = (
                f"Detected that the golden list contains "
                f"a different number of schemas than the current list.\n"
                f"Golden:\n[]\nCurrent:\n['{current_m}']\n"
                f"Please make sure each schema has a corresponding golden file,"
                f" and vice versa.\n")
            self.assertTrue(
                str(fail_on_changes([current_m], [])) == return_str)
            return_str = (
                f"Detected that the golden list contains "
                f"a different number of schemas than the current list.\n"
                f"Golden:\n['{golden_m}']\nCurrent:\n[]\n"
                f"Please make sure each schema has a corresponding golden file,"
                f" and vice versa.\n")
            self.assertTrue(
                str(fail_on_changes([], [golden_m])) == return_str)

            # Assert that files that would normally pass will fail
            # if the golden files do not have the .golden extension
            return_str = (
                f"Detected that filenames in the golden list, ['{current_m}'],"
                f" do not match the filenames in the current list, "
                f"['{current_m}'].\n"
                f"Please make sure each schema has a corresponding golden file,"
                f" and vice versa.\n")
            self.assertTrue(
                str(fail_on_changes([current_m], [current_m])) == return_str)


if __name__ == '__main__':
    unittest.main()
