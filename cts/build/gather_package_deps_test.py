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
    package_json_path = None
    meta_far_path = None
    output_dir = None

    def setUp(self):
        self.source_dir = tempfile.TemporaryDirectory()
        self.output_dir = tempfile.TemporaryDirectory()

        self.package_json_path = os.path.join(self.source_dir.name, 'pkg.json')
        self.meta_far_path = os.path.join(self.source_dir.name, 'meta.far')

        # Create placeholder files.
        open(self.package_json_path, 'a').close()
        open(self.meta_far_path, 'a').close()

    def tearDown(self):
        self.source_dir.cleanup()
        self.output_dir.cleanup()

    def test_init(self):
        GatherPackageDeps(
            self.package_json_path, self.meta_far_path, self.output_dir.name)

        with self.assertRaises(ValueError):
            GatherPackageDeps('', self.meta_far_path, self.output_dir.name)
        with self.assertRaises(ValueError):
            GatherPackageDeps(None, self.meta_far_path, self.output_dir.name)

        with self.assertRaises(ValueError):
            GatherPackageDeps(self.package_json_path, '', self.output_dir.name)
        with self.assertRaises(ValueError):
            GatherPackageDeps(
                self.package_json_path, None, self.output_dir.name)

        with self.assertRaises(ValueError):
            GatherPackageDeps(self.package_json_path, self.meta_far_path, '')
        with self.assertRaises(ValueError):
            GatherPackageDeps(self.package_json_path, self.meta_far_path, None)

    def test_parse_package_json(self):

        gatherer = GatherPackageDeps(
            self.package_json_path, self.meta_far_path, self.output_dir.name)

        with open(self.package_json_path, 'w') as f:
            f.write('noooot JSOOOOON')
        with self.assertRaises(ValueError):
            gatherer.parse_package_json()

        with open(self.package_json_path, 'w') as f:
            f.write(r'{ }')
        with self.assertRaises(KeyError):
            gatherer.parse_package_json()

        with open(self.package_json_path, 'w') as f:
            f.write(r'{ "blobs": [] }')
        manifest_dict = gatherer.parse_package_json()
        self.assertDictEqual(manifest_dict, {})

        with open(self.package_json_path, 'w') as f:
            f.write(
                """{ "blobs":
                        [ { "source_path": "some/path/A", "path": "path/A" } ]
                   }
                """)
        manifest_dict = gatherer.parse_package_json()
        self.assertDictEqual(manifest_dict, {'path/A': 'some/path/A'})

        with open(self.package_json_path, 'w') as f:
            f.write(
                """{ "blobs":
                        [
                            { "source_path": "some/path/A", "path": "path/A" },
                            { "source_path": "some/path/B", "path": "path/B" }
                        ]
                    }
                """)
        manifest_dict = gatherer.parse_package_json()
        self.assertDictEqual(
            manifest_dict, {
                'path/A': 'some/path/A',
                'path/B': 'some/path/B'
            })

    @unittest.mock.patch.object(shutil, 'copyfile', autospec=True)
    def test_copy_meta_far(self, copyfile_mock):
        gatherer = GatherPackageDeps(
            self.package_json_path, self.meta_far_path, self.output_dir.name)
        gatherer.copy_meta_far()
        copyfile_mock.assert_called_once_with(
            self.meta_far_path, os.path.join(self.output_dir.name, 'meta.far'))

    @unittest.mock.patch.object(shutil, 'copyfile', autospec=True)
    @unittest.mock.patch.object(os, 'makedirs', autospec=True)
    def test_copy_to_output_dir(self, makedirs_mock, copyfile_mock):
        gatherer = GatherPackageDeps(
            self.package_json_path, self.meta_far_path, self.output_dir.name)
        test_dict = {
            'path/A': '../../../thing/A',
            'path/B': 'thing/B',
            'path/to/C': '/abs/path/C'
        }
        gatherer.copy_to_output_dir(test_dict)

        self.assertEqual(3, makedirs_mock.call_count)
        self.assertEqual(3, copyfile_mock.call_count)

        self.assertDictEqual(
            test_dict, {
                'path/A': 'thing/A',
                'path/B': 'thing/B',
                'path/to/C': 'abs/path/C'
            })

    def test_write_new_manifest(self):

        gatherer = GatherPackageDeps(
            self.package_json_path, self.meta_far_path, self.output_dir.name)

        gatherer.write_new_manifest(
            {
                'path/A': 'some/path/A',
                'path/B': 'some/path/B'
            })
        with open(os.path.join(self.output_dir.name, 'package.manifest'),
                  'r') as f:
            manifest_data = f.read()
            self.assertIn('path/A=some/path/A', manifest_data)
            self.assertIn('path/B=some/path/B', manifest_data)
            self.assertIn('meta/package=meta.far', manifest_data)

    def test_archive_output(self):
        tar_path = os.path.join(self.output_dir.name, 'package.tar')
        self.assertFalse(os.path.isfile(tar_path))
        gatherer = GatherPackageDeps(
            self.package_json_path, self.meta_far_path, self.output_dir.name)
        gatherer.archive_output()
        self.assertTrue(os.path.isfile(tar_path))

        file_a = os.path.join(self.output_dir.name, 'fileA')
        file_b = os.path.join(self.output_dir.name, 'sub', 'fileB')
        file_c = os.path.join(self.output_dir.name, 'another', 'dir', 'fileC')
        with open(file_a, 'w') as f:
            f.write('A')
        os.makedirs(os.path.dirname(file_b), exist_ok=False)
        with open(file_b, 'w') as f:
            f.write('BB')
        os.makedirs(os.path.dirname(file_c), exist_ok=False)
        with open(file_c, 'w') as f:
            f.write('CCC')

        gatherer.archive_output()
        self.assertTrue(os.path.isfile(tar_path))

        # Main thing we need to check here is that the paths within the archive
        # are relative instead of absolute, because absolute paths relative to the
        # system that created the archive are meaningless for anyone unarchiving.
        size_index = {'./fileA': 1, 'sub/fileB': 2, 'another/dir/fileC': 3}
        with tarfile.open(tar_path, 'r') as tar:
            for member in tar.getmembers():
                self.assertIn(member.name, size_index)
                self.assertEqual(member.size, size_index[member.name])

    def test_run(self):

        backup_cwd = os.getcwd()
        os.chdir(self.source_dir.name)

        file_meta = 'meta.far'
        file_a = 'fileA'
        file_b = os.path.join('sub', 'fileB')
        file_c = os.path.join('another', 'dir', 'fileC')
        open(file_meta, 'a').close()
        open(file_a, 'a').close()
        os.makedirs(os.path.dirname(file_b), exist_ok=False)
        open(file_b, 'a').close()
        os.makedirs(os.path.dirname(file_c), exist_ok=False)
        open(file_c, 'a').close()

        with open(self.package_json_path, 'w') as f:
            f.write(
                """{{ "blobs":
                        [
                            {{ "source_path": "{}", "path": "path/A" }},
                            {{ "source_path": "{}", "path": "path/B" }},
                            {{ "source_path": "{}", "path": "path/C" }}
                        ]
                    }}
                """.format(file_a, file_b, file_c))

        gatherer = GatherPackageDeps(
            self.package_json_path, self.meta_far_path, self.output_dir.name)
        gatherer.run()
        expected_files = {
            './package.manifest', './meta.far', './fileA', 'sub/fileB',
            'another/dir/fileC'
        }
        observed_files = set()
        expected_manifest_lines = {
            'path/A=fileA', 'path/B=sub/fileB', 'path/C=another/dir/fileC',
            'meta/package=meta.far'
        }
        observed_manifest_lines = set()
        with tarfile.open(os.path.join(self.output_dir.name, 'package.tar'),
                          'r') as tar:
            for member in tar.getmembers():
                observed_files.add(member.name)
                if member.name == './package.manifest':
                    for line in tar.extractfile(member).readlines():
                        observed_manifest_lines.add(
                            line.decode("utf-8").strip())
        self.assertEqual(observed_files, expected_files)
        self.assertEqual(observed_manifest_lines, expected_manifest_lines)

        os.chdir(backup_cwd)


if __name__ == '__main__':
    unittest.main()
