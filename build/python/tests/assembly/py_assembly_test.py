# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""!/usr/bin/env python3.8"""

import unittest

from assembly import BlobEntry, PackageManifest, PackageMetaData
import serialization

raw_json = """{
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
        manifest = serialization.json_loads(PackageManifest, raw_json)

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
        self.assertEqual(serialized_json, raw_json)

    def test_serialize_blob(self):
        blob = BlobEntry(
            path="an/empty/file",
            merkle=
            "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b",
            size=0,
            source_path="source/path/to/an/empty/file")
        self.assertEqual(
            serialization.json_dumps(blob, indent=4), empty_blob_raw_json)
