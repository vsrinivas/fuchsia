# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""!/usr/bin/env python3.8"""

import json
import unittest

from assembly import BlobEntry, PackageManifest, PackageMetaData
from assembly.assembly_input_bundle import AssemblyInputBundle
from assembly.common import FileEntry
import serialization

raw_package_manifest_json = """{
    "package": {
        "name": "some_package",
        "version": "42"
    },
    "blobs": [
        {
            "path": "meta/",
            "merkle": "0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789ABCDEF",
            "size": 4096,
            "source_path": "some/source/path/to/a/file"
        },
        {
            "path": "a/file",
            "merkle": "123456789abcdef0123456789ABCDEF0123456789abcdef0123456789ABCDEF0",
            "size": 8192,
            "source_path": "some/other/source/path"
        },
        {
            "path": "an/empty/file",
            "merkle": "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b",
            "size": 0,
            "source_path": "source/path/to/an/empty/file"
        }
    ],
    "version": "1"
}"""

empty_blob_raw_json = """{
    "path": "an/empty/file",
    "merkle": "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b",
    "size": 0,
    "source_path": "source/path/to/an/empty/file"
}"""


class PackageManifestTest(unittest.TestCase):

    def test_deserialize_from_json(self):
        manifest = serialization.json_loads(
            PackageManifest, raw_package_manifest_json)

        self.assertEqual(
            manifest,
            PackageManifest(
                PackageMetaData("some_package", 42), [
                    BlobEntry(
                        path="meta/",
                        merkle=
                        "0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789ABCDEF",
                        size=4096,
                        source_path="some/source/path/to/a/file"),
                    BlobEntry(
                        path="a/file",
                        merkle=
                        "123456789abcdef0123456789ABCDEF0123456789abcdef0123456789ABCDEF0",
                        size=8192,
                        source_path="some/other/source/path"),
                    BlobEntry(
                        path="an/empty/file",
                        merkle=
                        "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b",
                        size=0,
                        source_path="source/path/to/an/empty/file"),
                ]))

    def test_serialize_json(self):
        manifest = PackageManifest(
            PackageMetaData("some_package", 42), [
                BlobEntry(
                    path="meta/",
                    merkle=
                    "0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789ABCDEF",
                    size=4096,
                    source_path="some/source/path/to/a/file"),
                BlobEntry(
                    path="a/file",
                    merkle=
                    "123456789abcdef0123456789ABCDEF0123456789abcdef0123456789ABCDEF0",
                    size=8192,
                    source_path="some/other/source/path"),
                BlobEntry(
                    path="an/empty/file",
                    merkle=
                    "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b",
                    size=0,
                    source_path="source/path/to/an/empty/file"),
            ])

        serialized_json = serialization.json_dumps(manifest, indent=4)
        self.maxDiff = None
        self.assertEqual(serialized_json, raw_package_manifest_json)

    def test_serialize_empty_blob(self):
        blob = BlobEntry(
            path="an/empty/file",
            merkle=
            "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b",
            size=0,
            source_path="source/path/to/an/empty/file")
        self.assertEqual(
            serialization.json_dumps(blob, indent=4), empty_blob_raw_json)


raw_assembly_input_bundle_json = """{
  "base": [
    "package1",
    "package2"
  ],
  "cache": [
    "package3",
    "package4"
  ],
  "system": [
    "package0"
  ],
  "kernel": {
    "path": "path/to/kernel",
    "args": [
      "arg1",
      "arg2"
    ],
    "clock_backstop": 1234
  },
  "boot_args": [
    "arg3",
    "arg4"
  ],
  "bootfs_files": [
    {
      "source": "path/to/source",
      "destination": "path/to/destination"
    }
  ],
  "bootfs_packages": [],
  "config_data": {
    "package1": [
      {
        "source": "path/to/source.json",
        "destination": "config.json"
      }
    ]
  },
  "blobs": []
}"""


class AssemblyInputBundleTest(unittest.TestCase):

    def test_serialization(self):

        self.maxDiff = None

        aib = AssemblyInputBundle()
        aib.base.update(["package1", "package2"])
        aib.cache.update(["package3", "package4"])
        aib.system.add("package0")
        aib.kernel.path = "path/to/kernel"
        aib.kernel.args.update(["arg1", "arg2"])
        aib.kernel.clock_backstop = 1234
        aib.boot_args.update(["arg3", "arg4"])
        aib.bootfs_files.add(FileEntry("path/to/source", "path/to/destination"))
        aib.config_data["package1"] = set(
            [FileEntry("path/to/source.json", "config.json")])

        self.assertEqual(
            aib.json_dumps(indent=2), raw_assembly_input_bundle_json)

    def test_deserialization(self):

        self.maxDiff = None

        aib = AssemblyInputBundle()
        aib.base.update(["package1", "package2"])
        aib.cache.update(["package3", "package4"])
        aib.system.add("package0")
        aib.kernel.path = "path/to/kernel"
        aib.kernel.args.update(["arg1", "arg2"])
        aib.kernel.clock_backstop = 1234
        aib.boot_args.update(["arg3", "arg4"])
        aib.bootfs_files.add(FileEntry("path/to/source", "path/to/destination"))
        aib.config_data["package1"] = set(
            [FileEntry("path/to/source.json", "config.json")])

        parsed_aib = AssemblyInputBundle.json_loads(
            raw_assembly_input_bundle_json)

        def assert_field_equal(parsed, expected, field_name):
            self.assertEqual(
                getattr(parsed, field_name), getattr(expected, field_name))

        assert_field_equal(parsed_aib, aib, "base")
        assert_field_equal(parsed_aib, aib, "cache")
        assert_field_equal(parsed_aib, aib, "system")
        assert_field_equal(parsed_aib, aib, "kernel")

        assert_field_equal(parsed_aib, aib, "boot_args")
        assert_field_equal(parsed_aib, aib, "bootfs_files")
        assert_field_equal(parsed_aib, aib, "config_data")

        self.assertEqual(parsed_aib, aib)
