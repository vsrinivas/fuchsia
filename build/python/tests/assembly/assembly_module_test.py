# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""!/usr/bin/env python3.8"""

import io
import os
import tempfile
from typing import List, Optional
import unittest

from assembly import AssemblyInputBundle, AIBCreator, BlobEntry
from assembly import FileEntry, FilePath, PackageManifest, PackageMetaData
from assembly.assembly_input_bundle import DriverDetails
import assembly
import serialization
from fast_copy_mock import mock_fast_copy_in

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
    "version": "1",
    "repository": "some_repo"
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
                ], "1", None, "some_repo"))

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
            ], "1", None, "some_repo")

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
  "blobs": [],
  "base_drivers": [
    {
      "package": "driver_package1",
      "components": [
        "meta/driver_component1.cm",
        "meta/driver_component2.cm"
      ]
    },
    {
      "package": "driver_package2",
      "components": [
        "meta/driver2_component1.cm",
        "meta/driver2_component2.cm"
      ]
    }
  ]
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
        aib.base_drivers = [
            DriverDetails(
                "driver_package1",
                set(["meta/driver_component1.cm",
                     "meta/driver_component2.cm"])),
            DriverDetails(
                "driver_package2",
                set(
                    [
                        "meta/driver2_component1.cm",
                        "meta/driver2_component2.cm"
                    ]))
        ]

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
        aib.base_drivers = [
            DriverDetails(
                "driver_package1",
                set(["meta/driver_component1.cm",
                     "meta/driver_component2.cm"])),
            DriverDetails(
                "driver_package2",
                set(
                    [
                        "meta/driver2_component1.cm",
                        "meta/driver2_component2.cm"
                    ]))
        ]

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
        assert_field_equal(parsed_aib, aib, "base_drivers")

        self.assertEqual(parsed_aib, aib)


def rebase_source(entry: FileEntry, path: str) -> FileEntry:
    return FileEntry(os.path.join(path, entry.source), entry.destination)


def rebase_destination(entry: FileEntry, path: str) -> FileEntry:
    return FileEntry(entry.source, os.path.join(path, entry.destination))


def format_merkle(value: int) -> str:
    return f"{value:064x}"


class PackageManifestBuilder:
    """Used to ease the creation of PackageManfests in tests, in a builder-like
    syntax:

      PackageManifestBuilder("some name").manifest_path(
          "where/to/put/the/manifest").fake_blob(20).blob(path="meta/").build()

    Which then creates a package manifest with two blobs, and writes it to the
    path given.
    """

    def __init__(self, name: str):
        self._manifest_path: Optional[FilePath] = None
        self._name = name
        self._blobs: List[BlobEntry] = []
        self._name_counter = 0

    def _make_blob_name(self, pattern: str = "blob_") -> str:
        name = f"{pattern}{self._name_counter}"
        self._name_counter += 1
        return name

    def _make_merkle(self, idx: Optional[int] = None) -> str:
        if idx is None:
            idx = self._name_counter
            self._name_counter += 1
        return format_merkle(idx)

    def manifest_path(self, path: FilePath) -> 'PackageManifestBuilder':
        """Set the path that the manifest should be written to."""
        self._manifest_path = path
        return self

    def blob(
            self,
            path: Optional[str] = None,
            merkle: Optional[str] = None,
            size: Optional[int] = None,
            source: Optional[FilePath] = None) -> 'PackageManifestBuilder':
        """Add a blob to the package, creating fake data for any fields that
        aren't given.
        """
        self._blobs.append(
            BlobEntry(
                path if path else f"some/path/for/{self._make_blob_name()}",
                merkle if merkle else self._make_merkle(), size if size else 0,
                source if source else
                f"source/path/for/{self._make_blob_name('input_')}"))
        return self

    def fake_blob(self, id: int) -> 'PackageManifestBuilder':
        """Add a completely faked blob to the package, using a numeric id to
        provide a way of tracking which package it belongs to.
        """
        self._blobs.append(
            BlobEntry(
                path=f"package/path/for/blob_{id}",
                merkle=self._make_merkle(id),
                size=id,
                source_path=f"source/path/for/input_{id}"))
        return self

    def build(self) -> PackageManifest:
        """Construct the PackageManifest object itself, and if a manifest path
        was provided, write out the manifest to that location.
        """
        manifest = PackageManifest(PackageMetaData(self._name), self._blobs)
        if self._manifest_path:
            with open(self._manifest_path, 'w') as manifest_file:
                serialization.json_dump(manifest, manifest_file)
        return manifest


class AIBCreatorTest(unittest.TestCase):

    def test_aib_creator_file_copy_and_package_manifest_relative_paths(self):
        """This tests that the AIBCreator will correctly copy the blobs/* files
           and create the package manifests in the correct location within the
           AIB structure, with package-manifest relative paths to the blobs.

           It also tests that the fini manifest and dep-files contain the
           correct paths.
        """
        # Diffs can be very large for some of these lists, so show the whole
        # thing.
        self.maxDiff = None

        # Mock out the copy routine so it doesn't fail, but we can see the ops
        # it would have made.
        (_, copies) = mock_fast_copy_in(assembly.assembly_input_bundle)

        outdir = tempfile.TemporaryDirectory()

        # save off the current cwd so that it can be restored later.
        curr_cwd = os.getcwd()
        try:
            # Switch into the tempdir to simulate a build environment.
            os.chdir(outdir.name)

            assembly_dir = "my_assembly"
            inputs_dir = "inputs"
            os.mkdir(assembly_dir)
            os.mkdir(inputs_dir)

            # Create two package manifests to use as base packages.  These will
            # be used to validate that blobs are copied to the right places, and
            # that the manifests are re-written correctly in a portable manner.
            some_package_manifest_path = "inputs/some_package_manifest.json"
            some_package_manifest = PackageManifestBuilder(
                "some_package"
            ).manifest_path(some_package_manifest_path).blob(
                path="meta/",
                merkle=
                "0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789ABCDEF",
                source="some/meta.far").fake_blob(11).fake_blob(12).fake_blob(
                    13).build()

            another_package_manifest_path = "inputs/another_package_manifest.json"
            another_package_manifest = PackageManifestBuilder(
                "another_package"
            ).manifest_path(another_package_manifest_path).blob(
                path="meta/",
                merkle=
                "123456789abcdef0123456789ABCDEF0123456789abcdef0123456789ABCDEF0",
                source="another/meta.far").fake_blob(26).fake_blob(27).build()
            created_manifests = [
                some_package_manifest, another_package_manifest
            ]

            # These are the files, and their sources, that we expect to find
            # copied into the AIB.  The destination paths here are the paths
            # used within the AIB, as that can then be used to compute the path
            # as seen from the cwd (which is what needs to be in the copy and
            # fini manifest operations).
            expected_files = [
                # some_package files
                FileEntry(
                    "some/meta.far",
                    "blobs/0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789ABCDEF"
                ),
                FileEntry(
                    "source/path/for/input_11", f"blobs/{format_merkle(11)}"),
                FileEntry(
                    "source/path/for/input_12", f"blobs/{format_merkle(12)}"),
                FileEntry(
                    "source/path/for/input_13", f"blobs/{format_merkle(13)}"),

                # another_package files
                FileEntry(
                    "another/meta.far",
                    "blobs/123456789abcdef0123456789ABCDEF0123456789abcdef0123456789ABCDEF0"
                ),
                FileEntry(
                    "source/path/for/input_26", f"blobs/{format_merkle(26)}"),
                FileEntry(
                    "source/path/for/input_27", f"blobs/{format_merkle(27)}")
            ]
            source_package_manifests = [
                some_package_manifest_path, another_package_manifest_path
            ]
            expected_package_manifests = [
                "packages/base/some_package", "packages/base/another_package"
            ]

            # Create the AIBCreator and perform the operation that's under test.
            aib_creator = AIBCreator(assembly_dir)
            aib_creator.base.update(
                [some_package_manifest_path, another_package_manifest_path])
            bundle, bundle_path, deps = aib_creator.build()

            # Verify that the bundle was written to the correct location (and it
            # matches the returned bundle).
            self.assertEqual(
                bundle_path, os.path.join(assembly_dir, "assembly_config.json"))
            with open(bundle_path) as bundle_file:
                parsed_bundle = AssemblyInputBundle.json_load(bundle_file)
                self.assertEqual(parsed_bundle, bundle)

            # Verify that resultant AIB contains the correct base packages.
            self.assertEqual(bundle.base, set(expected_package_manifests))

            # Verify that the package manfiests have been rewritten to use file-
            # relative blob paths into the correct directory.
            def validate_rewritten_package_manifest(
                    path: FilePath, expected: PackageManifest):
                """Parses the PackageManifest at the given path, and compares it
                with the `expected` one.
                """
                with open(path) as package_manifest_file:
                    parsed_manifest = serialization.json_load(
                        PackageManifest, package_manifest_file)

                self.assertEqual(parsed_manifest.package, expected.package)
                self.assertEqual(parsed_manifest.blob_sources_relative, "file")

                # The expected blobs are the passed-in set, but with the source
                # path set to be by merkle in the blobs/ dir of the AIB.
                expected_blobs = [
                    BlobEntry(
                        blob.path, blob.merkle, blob.size,
                        f"../../blobs/{blob.merkle}") for blob in expected.blobs
                ]
                self.assertEqual(parsed_manifest.blobs, expected_blobs)

            for (path, manifest) in zip(expected_package_manifests,
                                        created_manifests):
                validate_rewritten_package_manifest(
                    os.path.join(assembly_dir, path), manifest)

            # Verify that the copies that were performed by the mocked
            # `fast_copy()` fn are correct.
            #
            # The expected copies are the the expected_files, but the
            # destination path is rebased to the cwd (prepending `assembly_dir`)
            #
            expected_copies = [
                rebase_destination(entry, assembly_dir)
                for entry in expected_files
            ]
            self.assertEqual(sorted(copies), sorted(expected_copies))

            # Verify that the deps are correctly reported.
            #
            # All the source paths that files were copied from should be listed,
            # as well as the package manifest paths that were read to create the
            # in-AIB version of the manifests.
            expected_deps = [entry.source for entry in expected_files]
            expected_deps.extend(source_package_manifests)
            self.assertEqual(
                sorted(deps), sorted(expected_deps))  # type: ignore

            # Verify that the fini manifest created (used to create archives of
            # the AIB is correct).
            #
            # The fini manifest contains all the destination paths from
            # expected_files, for both source and destination, but assembly_dir
            # is prepended to all source paths to make them relative to the cwd.

            # Verify that all_file_paths() returns the correct (AIB-relative)
            # files.
            expected_paths = [entry.destination for entry in expected_files]
            expected_paths.extend(expected_package_manifests)
            self.assertEqual(
                sorted(bundle.all_file_paths()),
                sorted(expected_paths))  # type: ignore

            # Created the expected entries from the expected_paths by pre-
            # pending the assembly_dir to create the source path.
            expected_paths.append("assembly_config.json")
            expected_fini_contents = sorted(
                [f"{path}={assembly_dir}/{path}" for path in expected_paths])

            # Write the fini manifest to a string buffer
            fini_file = io.StringIO()
            bundle.write_fini_manifest(fini_file, base_dir=assembly_dir)

            # Parse the written buffer into lines to compare with the expected
            # entries.
            fini_entries = sorted(fini_file.getvalue().splitlines())
            self.assertEqual(fini_entries, expected_fini_contents)

        finally:
            os.chdir(curr_cwd)

            # Clean up the tempdir
            outdir.cleanup()
