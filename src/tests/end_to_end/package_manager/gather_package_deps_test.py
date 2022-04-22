#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import tempfile
import shutil
import tarfile
import unittest
from unittest import mock

from gather_package_deps import GatherPackageDeps


class GatherPackageDepsTests(unittest.TestCase):

    source_dir = None
    package_json = None
    meta_far = None

    output_dir = None
    output_tar = None
    depfile = None

    def setUp(self):
        self.source_dir = tempfile.TemporaryDirectory()
        self.package_json = os.path.join(self.source_dir.name, 'pkg.json')
        self.meta_far = os.path.join(self.source_dir.name, 'meta.far')

        self.output_dir = tempfile.TemporaryDirectory()
        self.depfile = os.path.join(self.output_dir.name, 'depfile.d')
        self.output_tar = os.path.join(self.output_dir.name, 'output.tar')

        # Create placeholder files.
        open(self.package_json, 'a').close()
        open(self.meta_far, 'a').close()

    def tearDown(self):
        self.source_dir.cleanup()
        self.output_dir.cleanup()

    def test_init(self):
        GatherPackageDeps(
            self.package_json, self.meta_far, self.output_tar, self.depfile)

        with self.assertRaises(ValueError):
            GatherPackageDeps('', self.meta_far, self.output_tar, self.depfile)
        with self.assertRaises(ValueError):
            GatherPackageDeps(
                None, self.meta_far, self.output_tar, self.depfile)

        with self.assertRaises(ValueError):
            GatherPackageDeps(
                self.package_json, '', self.output_tar, self.depfile)
        with self.assertRaises(ValueError):
            GatherPackageDeps(
                self.package_json, None, self.output_tar, self.depfile)

        with self.assertRaises(ValueError):
            GatherPackageDeps(
                self.package_json, self.meta_far, '', self.depfile)
        with self.assertRaises(ValueError):
            GatherPackageDeps(
                self.package_json, self.meta_far, None, self.depfile)

        with self.assertRaises(ValueError):
            GatherPackageDeps(
                self.package_json, self.meta_far, self.output_tar, '')
        with self.assertRaises(ValueError):
            GatherPackageDeps(
                self.package_json, self.meta_far, self.output_tar, None)

    def test_parse_package_json(self):

        gatherer = GatherPackageDeps(
            self.package_json, self.meta_far, self.output_tar, self.depfile)

        with open(self.package_json, 'w') as f:
            f.write('noooot JSOOOOON')
        with self.assertRaises(ValueError):
            gatherer.parse_package_json()

        with open(self.package_json, 'w') as f:
            f.write(r'{ }')
        with self.assertRaises(KeyError):
            gatherer.parse_package_json()

        with open(self.package_json, 'w') as f:
            f.write(r'{ "blobs": [] }')
        manifest_paths = gatherer.parse_package_json()
        self.assertEqual(manifest_paths, [])

        with open(self.package_json, 'w') as f:
            f.write(
                """{ "blobs":
                        [ { "source_path": "some/path/A", "path": "path/A" } ]
                   }
                """)
        manifest_paths = gatherer.parse_package_json()
        self.assertEqual(manifest_paths, [('path/A', 'some/path/A')])

        with open(self.package_json, 'w') as f:
            f.write(
                """{ "blobs":
                        [
                            { "source_path": "some/path/A", "path": "path/A" },
                            { "source_path": "some/path/B", "path": "path/B" }
                        ]
                    }
                """)
        manifest_paths = gatherer.parse_package_json()
        self.assertEqual(
            manifest_paths,
            [
                ('path/A', 'some/path/A'),
                ('path/B', 'some/path/B'),
            ],
        )

        with open(self.package_json, 'w') as f:
            f.write(
                """{ "blobs":
                        [
                            { "source_path": "/abs/path/to/A", "path": "path/A" },
                            { "source_path": "../../path/to/B", "path": "path/B" }
                        ]
                    }
                """)
        manifest_paths = gatherer.parse_package_json()
        self.assertEqual(
            manifest_paths,
            [
                ('path/A', '/abs/path/to/A'),
                ('path/B', '../../path/to/B'),
            ],
        )

    def test_create_archive(self):
        gatherer = GatherPackageDeps(
            self.package_json, self.meta_far, self.output_tar, self.depfile)
        gatherer.create_archive({})
        self.assertTrue(os.path.isfile(self.output_tar))

        file_a = os.path.join(self.source_dir.name, 'fileA')
        file_b = os.path.join(self.source_dir.name, 'sub', 'fileB')
        file_c = os.path.join(self.source_dir.name, 'another', 'dir', 'fileC')
        with open(file_a, 'w') as f:
            f.write('A')
        os.makedirs(os.path.dirname(file_b), exist_ok=False)
        with open(file_b, 'w') as f:
            f.write('BB')
        os.makedirs(os.path.dirname(file_c), exist_ok=False)
        with open(file_c, 'w') as f:
            f.write('CCC')

        manifest_paths = {
            ('path/A', file_a),
            ('path/B', file_b),
            ('path/C', file_c),
        }
        gatherer.create_archive(manifest_paths)
        self.assertTrue(os.path.isfile(self.output_tar))

        # Main thing we need to check here is that the paths within the archive
        # matches what's specified in manifest_paths.
        expected_size_index = {
            'path/A': 1,
            'path/B': 2,
            'path/C': 3,
            'meta.far': 0,
            'package.manifest': 64,
        }
        observed_size_index = {}
        with tarfile.open(self.output_tar, 'r') as tar:
            for member in tar.getmembers():
                observed_size_index[member.name] = member.size
        self.assertDictEqual(observed_size_index, expected_size_index)

    def test_run(self):

        backup_cwd = os.getcwd()
        os.chdir(self.source_dir.name)

        file_meta = 'meta.far'
        file_a = 'fileA'
        file_a_abs = os.path.abspath(file_a)
        file_b = os.path.join('sub', 'fileB')
        file_c = os.path.join('another', 'dir', 'fileC')
        open(file_meta, 'a').close()
        open(file_a, 'a').close()
        os.makedirs(os.path.dirname(file_b), exist_ok=False)
        open(file_b, 'a').close()
        os.makedirs(os.path.dirname(file_c), exist_ok=False)
        open(file_c, 'a').close()

        with open(self.package_json, 'w') as f:
            f.write(
                """{{ "blobs":
                        [
                            {{ "source_path": "{}", "path": "path/A" }},
                            {{ "source_path": "{}", "path": "path/B" }},
                            {{ "source_path": "{}", "path": "path/C" }}
                        ]
                    }}
                """.format(file_a_abs, file_b, file_c))

        gatherer = GatherPackageDeps(
            self.package_json, self.meta_far, self.output_tar, self.depfile)
        gatherer.run()
        expected_files = {
            'package.manifest',
            'meta.far',
            'path/A',
            'path/B',
            'path/C',
        }
        observed_files = set()
        expected_manifest_lines = {
            'path/A=path/A',
            'path/B=path/B',
            'path/C=path/C',
            'meta/package=meta.far',
        }
        observed_manifest_lines = set()
        with tarfile.open(self.output_tar, 'r') as tar:
            for member in tar.getmembers():
                observed_files.add(member.name)
                if member.name == 'package.manifest':
                    for line in tar.extractfile(member).readlines():
                        observed_manifest_lines.add(
                            line.decode("utf-8").strip())
        self.assertEqual(observed_files, expected_files)
        self.assertEqual(observed_manifest_lines, expected_manifest_lines)

        with open(self.depfile, 'r') as f:
            observed_depfile = f.read()
            expected_depfile = f'{self.output_tar}: fileA sub/fileB another/dir/fileC\n'
            self.assertEqual(observed_depfile, expected_depfile)

        os.chdir(backup_cwd)


if __name__ == '__main__':
    unittest.main()
