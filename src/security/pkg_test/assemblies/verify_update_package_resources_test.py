#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import unittest
from verify_update_package_resources import verify_update_package_resources


class VerifyUpdatePackageResourcesTests(unittest.TestCase):
    """
    Test behaviour of build-time utility that ensures that an update package
    manifest designates a subset of the blobs designated an ordinary package
    manifest.
    """

    update_package_manifest = json.loads(
        """
    {
        "version": "1",
        "package": {
            "name":"update",
            "version":"0"
        },
        "blobs": [
            {
                "source_path": "a",
                "path": "data/a",
                "merkle": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                "size": 0
            },
            {
                "source_path": "b",
                "path": "data/b",
                "merkle": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                "size": 1
            }
        ]
    }
    """)

    update_package_manifest_missing_merkle = json.loads(
        """
    {
        "version": "1",
        "package": {
            "name":"update",
            "version":"0"
        },
        "blobs": [
            {
                "source_path": "a",
                "path": "data/a",
                "merkle": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                "size": 0
            },
            {
                "source_path": "b",
                "path": "data/b",
                "size": 1
            }
        ]
    }
    """)

    same_package_manifest = json.loads(
        """
    {
        "version": "1",
        "package": {
            "name":"update_same",
            "version":"1"
        },
        "blobs": [
            {
                "source_path": "a_same",
                "path": "data/a_same",
                "merkle": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                "size": 1000
            },
            {
                "source_path": "b_same",
                "path": "data/b_same",
                "merkle": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                "size": 200
            }
        ]
    }
    """)

    extra_package_manifest = json.loads(
        """
    {
        "version": "1",
        "package": {
            "name":"update_extra",
            "version":"2"
        },
        "blobs": [
            {
                "source_path": "a_extra",
                "path": "data/a_extra",
                "merkle": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                "size": 20
            },
            {
                "source_path": "b_extra",
                "path": "data/b_extra",
                "merkle": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                "size": 91
            },
            {
                "source_path": "c_extra",
                "path": "data/c_extra",
                "merkle": "ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
                "size": 4
            }
        ]
    }
    """)

    missing_package_manifest = json.loads(
        """
    {
        "version": "1",
        "package": {
            "name": "update_missing",
            "version": "3"
        },
        "blobs": [
            {
                "source_path": "a_missing",
                "path": "data/a_missing",
                "merkle": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                "size": 747
            },
            {
                "source_path": "c_missing",
                "path": "data/c_missing",
                "merkle": "ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
                "size": 89
            }
        ]
    }
    """)

    bad_version_package_manifest = json.loads(
        """
    {
        "version": "2",
        "package": {
            "name":"update",
            "version":"0"
        },
        "blobs": [
            {
                "source_path": "a",
                "path": "data/a",
                "merkle": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                "size": 0
            },
            {
                "source_path": "b",
                "path": "data/b",
                "merkle": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                "size": 1
            }
        ]
    }
    """)

    bad_version_type_package_manifest = json.loads(
        """
    {
        "version": 1,
        "package": {
            "name":"update",
            "version":"0"
        },
        "blobs": [
            {
                "source_path": "a",
                "path": "data/a",
                "merkle": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                "size": 0
            },
            {
                "source_path": "b",
                "path": "data/b",
                "merkle": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                "size": 1
            }
        ]
    }
    """)

    def test_same(self):
        self.assertIsNone(
            verify_update_package_resources(
                VerifyUpdatePackageResourcesTests.same_package_manifest,
                VerifyUpdatePackageResourcesTests.update_package_manifest))

    def test_missing_merkle(self):
        self.assertRaises(
            Exception, lambda: verify_update_package_resources(
                VerifyUpdatePackageResourcesTests.same_package_manifest,
                VerifyUpdatePackageResourcesTests.
                update_package_manifest_missing_merkle))

    def test_extra(self):
        self.assertIsNone(
            verify_update_package_resources(
                VerifyUpdatePackageResourcesTests.extra_package_manifest,
                VerifyUpdatePackageResourcesTests.update_package_manifest))

    def test_missing(self):
        self.assertRaises(
            Exception, lambda: verify_update_package_resources(
                VerifyUpdatePackageResourcesTests.missing_package_manifest,
                VerifyUpdatePackageResourcesTests.update_package_manifest))

    def test_bad_version(self):
        self.assertRaises(
            ValueError, lambda: verify_update_package_resources(
                VerifyUpdatePackageResourcesTests.bad_version_package_manifest,
                VerifyUpdatePackageResourcesTests.update_package_manifest))

    def test_bad_version_type(self):
        self.assertRaises(
            ValueError, lambda: verify_update_package_resources(
                VerifyUpdatePackageResourcesTests.
                bad_version_type_package_manifest,
                VerifyUpdatePackageResourcesTests.update_package_manifest))


if __name__ == '__main__':
    unittest.main()
