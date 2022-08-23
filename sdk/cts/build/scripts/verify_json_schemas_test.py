#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for verify_json_schemas.py"""

from tempfile import TemporaryDirectory
import json
import unittest
from verify_json_schemas import (
    fail_on_breaking_changes, SchemaListMismatchError, MissingInputError,
    GoldenMismatchError)


class VerifyJsonSchemaTests(unittest.TestCase):

    def generate_path(self, base_dir, file_name):
        return base_dir + "/" + file_name

    def test_fail_on_breaking_changes(self):
        data_A = "some_data_A"
        data_B = "some_data_B"
        manifest = {"atoms": []}

        with TemporaryDirectory() as base_dir:
            golden_m = self.generate_path(base_dir, "core.golden")
            manifest['atoms'].append(data_A)
            with open(golden_m, 'w') as f:
                json.dump(manifest, f)
            current_m = self.generate_path(base_dir, "core")
            with open(current_m, 'w') as f:
                json.dump(manifest, f)
            # Assert that a normal case will pass.
            self.assertTrue(
                fail_on_breaking_changes([current_m], [golden_m]) is None)

            manifest['atoms'] = []
            manifest['atoms'].append(data_B)
            with open(current_m, 'w') as f:
                json.dump(manifest, f)
            # Assert that schemas with same keys, different values is a non-breaking change.
            self.assertRaises(
                GoldenMismatchError, fail_on_breaking_changes, [current_m],
                [golden_m])

            manifest2 = {"required": ["req1", "req2"]}
            with open(golden_m, 'w') as f:
                json.dump(manifest2, f)
            manifest2["required"].append("req3")
            with open(current_m, 'w') as f:
                json.dump(manifest2, f)
            # Assert that any changes to the values of "required" key are a breaking change.
            self.assertRaisesRegex(
                GoldenMismatchError,
                ".*'required' parameters changed on root.*",
                fail_on_breaking_changes, [current_m], [golden_m])

            manifest2["required"].remove("req2")
            manifest2["required"].remove("req3")
            with open(current_m, 'w') as f:
                json.dump(manifest2, f)
            self.assertRaisesRegex(
                GoldenMismatchError,
                ".*'required' parameters changed on root.*",
                fail_on_breaking_changes, [current_m], [golden_m])
            manifest2["required"] = "data_string"
            with open(current_m, 'w') as f:
                json.dump(manifest2, f)
            self.assertRaisesRegex(
                GoldenMismatchError,
                ".*'required' parameters changed on root.*",
                fail_on_breaking_changes, [current_m], [golden_m])

            manifest['atoms'].remove(data_B)
            with open(golden_m, 'w') as f:
                json.dump(manifest, f)
            manifest['next'] = {"some_key": "some value B"}
            with open(current_m, 'w') as f:
                json.dump(manifest, f)
            # Assert that a schema with a key addition at root is a non-breaking change.
            self.assertRaisesRegex(
                GoldenMismatchError, ".*New keys of root\:\n\s*\{'next'.*",
                fail_on_breaking_changes, [current_m], [golden_m])

            manifest['atoms'] = {"some_key": "some value B"}
            with open(current_m, 'w') as f:
                json.dump(manifest, f)
            # Assert that a schema with a key addition beneath root is a non-breaking change.
            self.assertRaisesRegex(
                GoldenMismatchError,
                ".*New keys of root.atoms\:\n\s*\{'some_key.*",
                fail_on_breaking_changes, [current_m], [golden_m])

            with open(golden_m, 'w') as f:
                json.dump(manifest, f)
            manifest2 = {
                "renamed_atoms": {
                    "some_key": "some value B",
                },
                "next": {
                    "some_key": "some value B",
                }
            }
            with open(current_m, 'w') as f:
                json.dump(manifest2, f)
            # Assert that a schema with a key renamed at root is a breaking change.
            self.assertRaisesRegex(
                GoldenMismatchError, ".*Missing keys of root\:\n\s*\{'atoms'.*",
                fail_on_breaking_changes, [current_m], [golden_m])

            manifest['atoms'] = {"re_key": "some value B"}
            with open(current_m, 'w') as f:
                json.dump(manifest, f)
            # Assert that a schema with a key renamed beneath root is a breaking change.
            self.assertRaisesRegex(
                GoldenMismatchError,
                ".*Missing keys of root.atoms\:\n\s*\{'some_key'.*",
                fail_on_breaking_changes, [current_m], [golden_m])

            manifest2 = {"next": {"some_key": "some value B",}}
            with open(current_m, 'w') as f:
                json.dump(manifest2, f)
            # Assert that a schema with a key deleted at root is a breaking change.
            self.assertRaisesRegex(
                GoldenMismatchError, ".*Missing keys of root\:\n\s*\{'atoms'.*",
                fail_on_breaking_changes, [current_m], [golden_m])

            manifest['atoms'] = []
            with open(current_m, 'w') as f:
                json.dump(manifest, f)
            # Assert that a schema with a key deleted beneath root is a breaking change.
            self.assertRaisesRegex(
                GoldenMismatchError,
                ".*Missing keys of root.atoms\:\n\s*\{'some_key'.*",
                fail_on_breaking_changes, [current_m], [golden_m])

            manifest = {
                'atoms': {
                    'some_key1': 'some value B'
                },
                'next': {
                    'some_key2': 'some value B'
                }
            }
            with open(golden_m, 'w') as f:
                json.dump(manifest, f)
            manifest = {
                'atoms': {
                    'some_key2': 'some value B'
                },
                'next': {
                    'some_key1': 'some value B'
                }
            }
            with open(current_m, 'w') as f:
                json.dump(manifest, f)
            # Assert that a schema with a key relocated at root is a breaking change.
            self.assertRaisesRegex(
                GoldenMismatchError,
                ".*Missing keys of root.next\:\n\s*\{'some_key2'.*",
                fail_on_breaking_changes, [current_m], [golden_m])
            self.assertRaisesRegex(
                GoldenMismatchError,
                ".*Missing keys of root.atoms\:\n\s*\{'some_key1'\}.*",
                fail_on_breaking_changes, [current_m], [golden_m])

            manifest = {
                'atoms': {
                    'some_key': {
                        "key1": 1
                    }
                },
                'next': {
                    'some_key': {
                        "key2": 2
                    }
                }
            }
            with open(golden_m, 'w') as f:
                json.dump(manifest, f)
            manifest = {
                'atoms': {
                    'some_key': {
                        "key2": 1
                    }
                },
                'next': {
                    'some_key': {
                        "key1": 2
                    }
                }
            }
            with open(current_m, 'w') as f:
                json.dump(manifest, f)
            # Assert that a schema with a key relocated beneath root is a breaking change.
            self.assertRaisesRegex(
                GoldenMismatchError,
                ".*Missing keys of root.atoms.some_key\:\n\s*\{'key1'.*",
                fail_on_breaking_changes, [current_m], [golden_m])
            self.assertRaisesRegex(
                GoldenMismatchError,
                ".*Missing keys of root.next.some_key\:\n\s*\{'key2'\}.*",
                fail_on_breaking_changes, [current_m], [golden_m])

            mock_gold = "path/to/core.golden"
            ret_str = (
                f"Detected missing JSON Schema {mock_gold}.\n"
                f"Please consult with sdk-dev@fuchsia.dev if you are"
                f" planning to remove a schema.\n"
                f"If you have approval to make this change, remove the "
                f"schema and corresponding golden file from the schema lists.\n"
            )
            # Assert that a nonexistent golden file will not pass.
            self.assertRaises(
                MissingInputError, fail_on_breaking_changes, [current_m],
                [mock_gold])
            self.assertTrue(
                str(MissingInputError(missing=mock_gold)) == ret_str)

            mock_curr = "path/to/core"
            ret_str = (
                f"Detected missing JSON Schema {mock_curr}.\n"
                f"Please consult with sdk-dev@fuchsia.dev if you are"
                f" planning to remove a schema.\n"
                f"If you have approval to make this change, remove the "
                f"schema and corresponding golden file from the schema lists.\n"
            )
            # Assert that a nonexistent current file will not pass.
            self.assertRaises(
                MissingInputError, fail_on_breaking_changes, [mock_curr],
                [golden_m])
            self.assertTrue(
                str(MissingInputError(missing=mock_curr)) == ret_str)

            # Assert that lists of a different size will not pass.
            ret_str = (
                f"Detected that the golden list contains "
                f"a different number of schemas than the current list.\n"
                f"Golden:\n['{golden_m}']\nCurrent:\n['{mock_curr}', '{golden_m}']\n"
                f"Please make sure each schema has a corresponding golden file,"
                f" and vice versa.\n")
            self.assertRaises(
                SchemaListMismatchError, fail_on_breaking_changes,
                [mock_curr, current_m], [golden_m])
            self.assertTrue(
                str(
                    SchemaListMismatchError(
                        err_type=0,
                        goldens=[golden_m],
                        currents=[mock_curr, golden_m])) == ret_str)
            self.assertRaises(
                SchemaListMismatchError, fail_on_breaking_changes, [current_m],
                [mock_gold, golden_m])
            ret_str = (
                f"Detected that the golden list contains "
                f"a different number of schemas than the current list.\n"
                f"Golden:\n[]\nCurrent:\n['{current_m}']\n"
                f"Please make sure each schema has a corresponding golden file,"
                f" and vice versa.\n")
            self.assertRaises(
                SchemaListMismatchError, fail_on_breaking_changes, [current_m],
                [])
            self.assertTrue(
                str(
                    SchemaListMismatchError(
                        err_type=0, goldens=[], currents=[current_m])) ==
                ret_str)
            ret_str = (
                f"Detected that the golden list contains "
                f"a different number of schemas than the current list.\n"
                f"Golden:\n['{golden_m}']\nCurrent:\n[]\n"
                f"Please make sure each schema has a corresponding golden file,"
                f" and vice versa.\n")
            self.assertRaises(
                SchemaListMismatchError, fail_on_breaking_changes, [],
                [golden_m])
            self.assertTrue(
                str(
                    SchemaListMismatchError(
                        err_type=0, goldens=[golden_m], currents=[])) ==
                ret_str)

            ret_str = (
                f"Detected that filenames in the golden list, ['{current_m}'],"
                f" do not correspond to those in the current list, "
                f"['{current_m}'].\n"
                f"Please make sure each schema has a corresponding golden file,"
                f" and vice versa.\n")
            # Assert that files that would normally pass will fail
            # if the golden files do not have the .golden extension.
            self.assertRaises(
                SchemaListMismatchError, fail_on_breaking_changes, [current_m],
                [current_m])
            self.assertTrue(
                str(
                    SchemaListMismatchError(
                        err_type=1, goldens=[current_m], currents=[current_m]))
                == ret_str)


if __name__ == '__main__':
    unittest.main()
