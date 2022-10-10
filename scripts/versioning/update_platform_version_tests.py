#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import filecmp
import json
import os
import shutil
import tempfile
import unittest
import update_platform_version

FAKE_VERSION_HISTORY_FILE_CONTENT = """{
    \"data\": {
        "name": "Platform version map",
        "type": "version_history",
        "versions": [
            {
                "api_level": "1",
                "abi_revision": "0x201665C5B012BA43"
            }
        ]
    },
    "schema_id": "https://fuchsia.dev/schema/version_history-ef02ef45.json"
}
"""

OLD_API_LEVEL = 1
OLD_SUPPORTED_API_LEVELS = [1]

NEW_API_LEVEL = 2
# This script doesn't update the set of supported API levels, this only happen
# when freezing an API level.
NEW_SUPPORTED_API_LEVELS = OLD_SUPPORTED_API_LEVELS

FAKE_SRC_FILE_CONTENT = "{ test }"
EXPECTED_DST_FILE_CONTENT = "{ test }"

class TestUpdatePlatformVersionMethods(unittest.TestCase):

    def setUp(self):
        self.test_dir = tempfile.mkdtemp()
        self.fake_version_history_file = os.path.join(
            self.test_dir, 'version_history.json')
        with open(self.fake_version_history_file, 'w') as f:
            f.write(FAKE_VERSION_HISTORY_FILE_CONTENT)
        update_platform_version.VERSION_HISTORY_PATH = self.fake_version_history_file
        self.fake_milestone_version_file = os.path.join(
            self.test_dir, 'platform_version.json')
        with open(self.fake_milestone_version_file, 'w') as f:
            pv = {
                'in_development_api_level': OLD_API_LEVEL,
                'supported_fuchsia_api_levels': OLD_SUPPORTED_API_LEVELS,
            }
            json.dump(pv, f)
        update_platform_version.PLATFORM_VERSION_PATH = self.fake_milestone_version_file

        self.test_src_dir = tempfile.mkdtemp()
        self.fake_src_file = os.path.join(
            self.test_src_dir, 'fuchsia_src.test.json')
        with open(self.fake_src_file, 'w') as f:
            f.write(FAKE_SRC_FILE_CONTENT)

        self.test_dst_dir = tempfile.mkdtemp()
        self.fake_dst_file = os.path.join(
            self.test_dst_dir, 'fuchsia_dst.test.json')

        self.test_dir = tempfile.mkdtemp()
        self.fake_golden_file = os.path.join(
            self.test_dir, 'compatibility_testing_goldens.json')


        content = [
            {
            'dst': self.fake_dst_file,
            'src': self.fake_src_file,
            },
        ]
        with open(self.fake_golden_file, 'w') as f:
            json.dump(content, f)

    def tearDown(self):
        shutil.rmtree(self.test_dir)

    def _version_history_contains_entry_for_api_level(self, api_level):
        with open(self.fake_version_history_file, "r") as f:
            version_history = json.load(f)
            versions = version_history['data']['versions']
            return any(
                version['api_level'] == str(api_level) for version in versions)

    def test_update_version_history(self):
        self.assertFalse(
            self._version_history_contains_entry_for_api_level(NEW_API_LEVEL))
        self.assertTrue(
            update_platform_version.update_version_history(NEW_API_LEVEL))
        self.assertTrue(
            self._version_history_contains_entry_for_api_level(NEW_API_LEVEL))
        self.assertFalse(
            update_platform_version.update_version_history(NEW_API_LEVEL))

    def _get_platform_version(self):
        with open(self.fake_milestone_version_file) as f:
            return json.load(f)

    def test_update_platform_version(self):
        pv = self._get_platform_version()
        self.assertNotEqual(NEW_API_LEVEL, pv['in_development_api_level'])

        self.assertTrue(
            update_platform_version.update_platform_version(NEW_API_LEVEL))

        pv = self._get_platform_version()
        self.assertEqual(NEW_API_LEVEL, pv['in_development_api_level'])
        self.assertEqual(
            NEW_SUPPORTED_API_LEVELS, pv['supported_fuchsia_api_levels'])

    def test_move_compatibility_test_goldens(self):
        self.assertTrue(update_platform_version.copy_compatibility_test_goldens(self.test_dir, NEW_API_LEVEL))
        self.assertTrue(filecmp.cmp(self.fake_src_file, self.fake_dst_file))



if __name__ == '__main__':
    unittest.main()
