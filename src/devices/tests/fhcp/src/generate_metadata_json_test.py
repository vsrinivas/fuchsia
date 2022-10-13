# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.Z
"""Unit tests for generate_metadata_json.py"""
import json
import os
import tempfile
import unittest
from generate_metadata_json import (convert_to_final_dict, validate)


class GenerateMetadataTests(unittest.TestCase):

    def test_convert_to_final_dict(self):
        url = "fuchsia-pkg://abc/def#meta/ghi"
        identifier = "a-matching-id"
        appendix_data = {"appendix": {"stuff": {}}}
        intermediate_data = [
            {
                "environments":
                    [{
                        "dimensions": {
                            "device_type": "unused"
                        },
                        "is_emu": True
                    }],
                "test":
                    {
                        "build_rule": "unused",
                        "cpu": "unused",
                        "has_generated_manifest": True,
                        "label": "unused",
                        "log_settings": {
                            "max_severity": "unused"
                        },
                        "name": "unused",
                        "os": "unused",
                        "package_label": identifier,
                        "package_manifests": ["unused"],
                        "package_url": url,
                        "wrapped_legacy_test": False
                    }
            }, {
                "test_types": ["a", "b"],
                "id":
                    identifier,
                "device_categories": [{
                    "category": "d",
                    "subcategory": "e"
                }],
                "environments":
                    [
                        {
                            "dimensions": {
                                "device_type": "f"
                            },
                            "tags": ["fhcp-automated"]
                        }
                    ]
            }
        ]
        expected = {
            "appendix": {
                "stuff": {}
            },
            "tests":
                [
                    {
                        "url": url,
                        "test_types": ["a", "b"],
                        "device_categories":
                            [{
                                "category": "d",
                                "subcategory": "e"
                            }],
                        "is_automated": True,
                    }
                ]
        }
        self.assertEqual(
            convert_to_final_dict(appendix_data, intermediate_data), expected)

        intermediate_data[1]["id"] = "not_matching"
        with self.assertRaises(ValueError) as ctx:
            convert_to_final_dict(appendix_data, intermediate_data)
        self.assertEqual(
            f"Did not find 'not_matching' in the tests.", str(ctx.exception))

        intermediate_data[1]["id"] = identifier
        ret = convert_to_final_dict(appendix_data, intermediate_data)
        self.assertTrue(ret["tests"][0]["is_automated"])

        intermediate_data[1]["environments"][0]["tags"] = []
        with self.assertRaises(ValueError) as ctx:
            convert_to_final_dict(appendix_data, intermediate_data)
        self.assertEqual(
            f"(\"The 'tags' field must have at least one tag of either 'fhcp-automated' or 'fhcp-manual'. Missing from:\", [])",
            str(ctx.exception))

        intermediate_data[1]["environments"][0]["tags"] = ["fhcp-manual"]
        ret = convert_to_final_dict(appendix_data, intermediate_data)
        self.assertFalse(ret["tests"][0]["is_automated"])

        invalid_data = {"test": {}}
        intermediate_data.append(invalid_data)
        with self.assertRaises(ValueError) as ctx:
            convert_to_final_dict(appendix_data, intermediate_data)
        self.assertEqual(
            f"('This is not a valid entry:', {invalid_data})",
            str(ctx.exception))

    def test_validate(self):
        url = "foo"
        url2 = "bar"
        data = {
            "certification_type": {
                "device_driver": {},
            },
            "system_types": {
                "workstation": {},
            },
            "driver_test_types":
                {
                    "generic_driver_tests": {},
                    "functional": {},
                    "spec_compliance": {},
                    "performance": {},
                    "power": {},
                    "security": {},
                    "hardware": {}
                },
            "device_category_types":
                {
                    "connectivity": {
                        "wifi": {},
                        "ethernet": {}
                    },
                    "input": {
                        "touchpad": {},
                        "mouse": {}
                    },
                },
            "tests":
                [
                    {
                        # This valid entry tests that validate() will loop
                        # through each entry by only having the 2nd entry
                        # have failures.
                        "url": url,
                        "test_types": ["functional"],
                        "device_categories":
                            [
                                {
                                    "category": "connectivity",
                                    "subcategory": "wifi"
                                }
                            ],
                        "is_automated": False,
                    },
                    {
                        "url": url2,
                        "test_types": [],
                        "device_categories": [],
                    }
                ]
        }
        with self.assertRaises(ValueError) as ctx:
            validate(data)
        self.assertEqual(
            f"The test {url2} must specify at least one category.",
            str(ctx.exception))

        data["tests"][1]["test_types"].append("functional")
        with self.assertRaises(ValueError) as ctx:
            validate(data)
        self.assertEqual(
            f"The test {url2} must specify at least one type.",
            str(ctx.exception))

        data["tests"][1]["device_categories"].append(
            {
                "category": "input",
                "subcategory": "touchpad"
            })
        with self.assertRaises(ValueError) as ctx:
            validate(data)
        self.assertEqual(
            f"The test {url2} must specify an 'is_automated' value.",
            str(ctx.exception))

        data["tests"][1]["is_automated"] = True
        self.assertTrue(validate(data))

        data["tests"][1]["device_categories"].append(
            {
                "category": "foo",
                "subcategory": "bar"
            })
        with self.assertRaises(ValueError) as ctx:
            validate(data)
        self.assertEqual(
            f"The test {url2} specifies an invalid type 'foo'.",
            str(ctx.exception))

        data["tests"][1]["device_categories"] = [
            {
                "category": "input",
                "subcategory": "foo"
            }
        ]
        with self.assertRaises(ValueError) as ctx:
            validate(data)
        self.assertEqual(
            f"The test {url2} specifies an invalid sub-type 'foo'.",
            str(ctx.exception))

        data["tests"][1]["test_types"].append("z")
        with self.assertRaises(ValueError) as ctx:
            validate(data)
        self.assertEqual(
            f"The test {url2} specifies an invalid category 'z'.",
            str(ctx.exception))
