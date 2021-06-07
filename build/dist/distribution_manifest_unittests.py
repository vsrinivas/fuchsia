#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Unit tests for the distribution_manifest.py Python module."""

import json
import os
import tempfile
import unittest

# From current directory
import distribution_manifest as dm

Entry = dm.Entry


class TestExpandManifestItems(unittest.TestCase):

    def test_regular_entries(self):
        input = [
            {
                'source': 'some/file',
                'destination': 'bin/foo',
                'label': '//src/foo',
            },
            {
                'source': 'other/stuff',
                'destination': 'bin/bar',
                'label': '//src/bar',
            },
        ]

        opened_files = set()
        result = dm.expand_manifest_items(input, opened_files)

        expected = [
            Entry(destination='bin/foo', source='some/file', label='//src/foo'),
            Entry(
                destination='bin/bar', source='other/stuff', label='//src/bar'),
        ]

        self.assertListEqual(result, expected)
        self.assertEqual(opened_files, set())

    def test_default_label(self):
        input = [
            {
                'source': 'some/file',
                'destination': 'bin/foo',
            },
            {
                'source': 'other/stuff',
                'destination': 'bin/bar',
                'label': '//src/bar',
            },
        ]

        opened_files = set()
        result = dm.expand_manifest_items(
            input, opened_files, default_label='//default')

        expected = [
            Entry(source='some/file', destination='bin/foo', label='//default'),
            Entry(
                source='other/stuff', destination='bin/bar', label='//src/bar'),
        ]

        self.assertListEqual(result, expected)
        self.assertEqual(opened_files, set())

    def test_file_entries(self):
        with tempfile.TemporaryDirectory() as tmpdirname:
            inner_file1 = os.path.join(tmpdirname, '1.dist')
            inner1 = [
                {
                    'source': 'some/file',
                    'destination': 'bin/foo',
                },
            ]
            with open(inner_file1, 'w') as f:
                json.dump(inner1, f)

            inner_file2 = os.path.join(tmpdirname, '2.dist')
            inner2 = [
                {
                    'file': inner_file1,
                    'label': '//inner2',
                },
            ]
            with open(inner_file2, 'w') as f:
                json.dump(inner2, f)

            input = [
                {
                    'file': inner_file2,
                    'label': '//default',
                },
            ]

            opened_files = set()
            result = dm.expand_manifest_items(input, opened_files, '//other')

            expected = [
                Entry(
                    source='some/file', destination='bin/foo',
                    label='//inner2'),
            ]

            self.assertListEqual(result, expected)
            self.assertSetEqual(opened_files, {inner_file1, inner_file2})


class TestExpandManifest(unittest.TestCase):

    def test_simple_case_no_conflicts(self):
        input = [
            {
                'source': 'some/file',
                'destination': 'bin/foo',
                'label': '//src/foo',
            },
            {
                'source': 'other/stuff',
                'destination': 'bin/bar',
                'label': '//src/bar',
            },
        ]
        opened_files = set()
        result, error = dm.expand_manifest(input, opened_files)

        expected = [
            Entry(
                destination='bin/bar', source='other/stuff', label='//src/bar'),
            Entry(destination='bin/foo', source='some/file', label='//src/foo'),
        ]

        self.assertListEqual(result, expected)
        self.assertEqual(opened_files, set())
        self.assertFalse(error)

    def test_duplicates_same_path(self):
        input = [
            {
                'source': 'some/file',
                'destination': 'bin/foo',
                'label': '//src/foo',
            },
            {
                'source': 'other/stuff',
                'destination': 'bin/bar',
                'label': '//src/bar',
            },
            {
                'source': 'some/file',
                'destination': 'bin/foo',
                'label': '//src/foo',
            },
        ]
        opened_files = set()
        result, error = dm.expand_manifest(input, opened_files)

        expected = [
            Entry(
                destination='bin/bar', source='other/stuff', label='//src/bar'),
            Entry(destination='bin/foo', source='some/file', label='//src/foo'),
        ]

        self.assertListEqual(result, expected)
        self.assertEqual(opened_files, set())
        self.assertFalse(error)

    def test_duplicates_same_content(self):
        with tempfile.TemporaryDirectory() as tmpdirname:
            # Create two different files with the same content.
            content = 'Some test data'
            file1_path = os.path.join(tmpdirname, 'file1')
            file2_path = os.path.join(tmpdirname, 'file2')
            with open(file1_path, 'w') as f:
                f.write(content)
            with open(file2_path, 'w') as f:
                f.write(content)

            input = [
                {
                    'source': file1_path,
                    'destination': 'bin/foo',
                    'label': '//src/foo',
                },
                {
                    'source': 'other/stuff',
                    'destination': 'bin/bar',
                    'label': '//src/bar',
                },
                {
                    'source': file2_path,
                    'destination': 'bin/foo',
                    'label': '//src/foo',
                },
            ]
            opened_files = set()
            result, error = dm.expand_manifest(input, opened_files)

            expected = [
                Entry(
                    destination='bin/bar',
                    source='other/stuff',
                    label='//src/bar'),
                Entry(
                    destination='bin/foo', source=file1_path,
                    label='//src/foo'),
            ]

            self.assertListEqual(result, expected)
            self.assertEqual(opened_files, {file1_path, file2_path})
            self.assertFalse(error)

    def test_duplicates_source_conflict(self):
        with tempfile.TemporaryDirectory() as tmpdirname:
            # Create two different files with different content.
            content = 'Some test data'
            file1_path = os.path.join(tmpdirname, 'file1')
            file2_path = os.path.join(tmpdirname, 'file2')
            with open(file1_path, 'w') as f:
                f.write(content)
            with open(file2_path, 'w') as f:
                f.write(content + '!')

            input = [
                {
                    'source': file1_path,
                    'destination': 'bin/foo',
                    'label': '//src/foo1',
                },
                {
                    'source': 'other/stuff',
                    'destination': 'bin/bar',
                    'label': '//src/bar',
                },
                {
                    'source': file2_path,
                    'destination': 'bin/foo',
                    'label': '//src/foo2',
                },
            ]
            opened_files = set()
            result, error = dm.expand_manifest(input, opened_files)

            expected = [
                Entry(
                    destination='bin/bar',
                    source='other/stuff',
                    label='//src/bar'),
                Entry(
                    destination='bin/foo',
                    source=file1_path,
                    label='//src/foo1'),
            ]

            self.assertListEqual(result, expected)
            self.assertEqual(opened_files, {file1_path, file2_path})
            expected_error = 'ERROR: Conflicting distribution entries!\n'
            expected_error += '  Conflicting source paths for destination path: bin/foo\n'
            expected_error += '   - source=%s label=//src/foo1\n' % file1_path
            expected_error += '   - source=%s label=//src/foo2\n' % file2_path

            self.assertEqual(error, expected_error)


class TestVerifyElfDependencies(unittest.TestCase):

    def test_simple_dependencies(self):
        binary = 'bin/foo'
        binary_deps = ['libbar.so', 'libc.so']
        lib_dir = 'LIB'

        def get_deps(lib_path):
            _DEPS_MAP = {
                'LIB/ld.so.1': [],
                'LIB/libbar.so': ['libzoo.so'],
                'LIB/libzoo.so': ['libc.so'],
            }
            return _DEPS_MAP.get(lib_path, None)

        visited_libs = set()
        error = dm.verify_elf_dependencies(
            binary, lib_dir, binary_deps, get_deps, visited_libs)
        self.assertFalse(error)
        self.assertSetEqual(
            visited_libs, {'LIB/libbar.so', 'LIB/libzoo.so', 'LIB/ld.so.1'})

    def test_circular_dependencies(self):
        binary = 'bin/foo'
        binary_deps = ['libbar.so', 'libc.so']
        lib_dir = 'LIB'

        def get_deps(lib_path):
            _DEPS_MAP = {
                'LIB/ld.so.1': [],
                'LIB/libbar.so': ['libzoo.so'],
                'LIB/libzoo.so': ['libbar.so', 'libc.so'],
            }
            return _DEPS_MAP.get(lib_path, None)

        visited_libs = set()
        error = dm.verify_elf_dependencies(
            binary, lib_dir, binary_deps, get_deps, visited_libs)
        self.assertFalse(error)
        self.assertSetEqual(
            visited_libs, {'LIB/libbar.so', 'LIB/libzoo.so', 'LIB/ld.so.1'})

    def test_missing_dependencies(self):
        binary = 'bin/foo'
        binary_deps = ['libbar.so', 'libc.so']
        lib_dir = 'LIB'

        def get_deps(lib_path):
            _DEPS_MAP = {
                'LIB/ld.so.1': [],
                'LIB/libbar.so': ['libzoo.so'],
                'LIB/libzoo.so': ['libmissing.so', 'libc.so'],
            }
            return _DEPS_MAP.get(lib_path, None)

        visited_libs = set()
        error = dm.verify_elf_dependencies(
            binary, lib_dir, binary_deps, get_deps, visited_libs)

        expected_error = [
            '%s missing dependency %s/libmissing.so' % (binary, lib_dir)
        ]
        self.assertListEqual(error, expected_error)
        self.assertSetEqual(
            visited_libs, {'LIB/libbar.so', 'LIB/libzoo.so', 'LIB/ld.so.1'})


if __name__ == "__main__":
    unittest.main()
