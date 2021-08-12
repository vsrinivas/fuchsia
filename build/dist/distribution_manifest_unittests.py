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

    def test_regular_entries_with_elf_runtime_dir(self):
        input = [
            {
                'source': 'some/file',
                'destination': 'bin/foo',
                'label': '//src/foo',
                'elf_runtime_dir': 'lib',
            },
            {
                'source': 'other/stuff',
                'destination': 'bin/bar',
                'label': '//src/bar',
                'elf_runtime_dir': 'lib/asan',
            },
            {
                'source': 'other/stuff2',
                'destination': 'bin/tool',
                'label': '//src/tool',
            },
        ]

        opened_files = set()
        result = dm.expand_partial_manifest_items(input, opened_files)

        # Here the elf_runtime_dir should be ignored / removed from the output
        expected = [
            Entry(destination='bin/foo', source='some/file', label='//src/foo'),
            Entry(
                destination='bin/bar', source='other/stuff', label='//src/bar'),
            Entry(
                destination='bin/tool',
                source='other/stuff2',
                label='//src/tool'),
        ]

        self.assertListEqual(result.entries, expected)

        expected_elf_runtime_map = {
            'bin/foo': 'lib',
            'bin/bar': 'lib/asan',
        }

        self.assertFalse(result.errors)
        self.assertDictEqual(result.elf_runtime_map, expected_elf_runtime_map)
        self.assertEqual(opened_files, set())

    def test_regular_entries_with_elf_runtime_dir_conflicts(self):
        input = [
            {
                'source': 'some/file',
                'destination': 'bin/foo',
                'label': '//src/foo',
                'elf_runtime_dir': 'lib',
            },
            {
                'source': 'some/alt/file',
                'destination': 'bin/foo',
                'label': '//src/foo',
                'elf_runtime_dir': 'lib/alt',
            },
        ]

        opened_files = set()
        result = dm.expand_partial_manifest_items(input, opened_files)

        expected_errors = [
            'ERROR: Entries with same destination path have different ELF runtime dir:',
            '  - destination=bin/foo source=some/file label=//src/foo elf_runtime_dir=lib',
            '  - destination=bin/foo source=some/alt/file label=//src/foo elf_runtime_dir=lib/alt',
        ]
        self.assertEqual(opened_files, set())
        self.assertListEqual(result.errors, expected_errors)

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

    def test_renamed_entries(self):
        input = [
            {
                'destination': 'bin/foo',
                'source': 'some/file',
                'label': '//src/foo',
            },
            {
                'destination': 'bin/bar',
                'renamed_source': 'some/file',
            },
            {
                'destination': 'bin/zoo',
                'renamed_source': 'some/file',
                'label': '//other',
            },
        ]
        opened_files = set()
        result, error = dm.expand_manifest(input, opened_files)

        expected = [
            Entry(destination='bin/bar', source='some/file', label='//src/foo'),
            Entry(destination='bin/zoo', source='some/file', label='//other'),
        ]
        self.assertListEqual(result, expected)
        self.assertEqual(opened_files, set())
        self.assertFalse(error)

    def test_renamed_entries_with_persistent_entry(self):
        input = [
            {
                'destination': 'bin/foo',
                'source': 'some/file',
                'label': '//src/foo',
            },
            {
                'destination': 'bin/bar',
                'renamed_source': 'some/file',
                'label': '//src/bar',
                'keep_original': True,
            },
        ]
        opened_files = set()
        result, error = dm.expand_manifest(input, opened_files)

        expected = [
            Entry(destination='bin/bar', source='some/file', label='//src/bar'),
            Entry(destination='bin/foo', source='some/file', label='//src/foo'),
        ]
        self.assertListEqual(result, expected)
        self.assertEqual(opened_files, set())
        self.assertFalse(error)

    def test_renamed_entries_with_errors(self):
        input = [
            {
                'destination': 'bin/foo',
                'source': 'some/foo',
                'label': '//src/foo',
            },
            {
                'destination': 'bin/bar',
                'renamed_source': 'something/missing',
            },
            {
                'destination': 'bin/zoo',
                'renamed_source': 'some/foo',
                'label': '//other',
            },
            {
                'destination': 'bin/tool',
                'renamed_source': 'some/foo2',
            },
        ]
        opened_files = set()
        result = dm.expand_partial_manifest_items(input, opened_files)

        expected_errors = []
        expected_errors += [
            'ERROR: Renamed distribution entries have unknown source destination:'
        ]
        expected_errors += [
            '  - {"destination": "bin/bar", "renamed_source": "something/missing"}'
        ]
        expected_errors += [
            '  - {"destination": "bin/tool", "renamed_source": "some/foo2"}'
        ]
        self.assertEqual(result.errors, expected_errors)
        self.assertEqual(opened_files, set())

    def test_renamed_entries_with_resource_errors(self):
        input = [
            {
                'destination': 'bin/foo',
                'source': 'some/foo',
                'label': '//src/foo',
            },
            {
                'destination': 'bin/bar',
                'source': 'some/foo',
                'label': '//src/bar'
            },
            {
                'destination': 'bin/zoo',
                'renamed_source': 'some/foo',
                'label': '//other',
            },
        ]
        opened_files = set()
        result = dm.expand_partial_manifest_items(input, opened_files)

        expected_errors = []
        expected_errors += [
            'ERROR: Multiple regular entries with the same source path:',
            '  - destination=bin/bar source=some/foo label=//src/bar',
            '  - destination=bin/foo source=some/foo label=//src/foo',
        ]
        expected_errors.append(
            '\nThis generally means a mix of renamed_binary() and resource() targets\n'
            +
            'that reference the same source. Try replacing the resource() targets by\n'
            + 'renamed_binary() ones to fix the problem\n')

        self.assertEqual(result.errors, expected_errors)
        self.assertEqual(opened_files, set())

    def test_renamed_entries_with_copy(self):
        input = [
            {
                'destination': 'bin/foo',
                'source': 'some-variant/foo',
                'label': '//src/foo(variant)',
            },
            {
                'copy_from': 'some-variant/foo',
                'copy_to': 'foo',
                'label': '//src/foo',
            },
            {
                'destination': 'bin/foo_renamed',
                'renamed_source': 'foo',
            },
        ]
        opened_files = set()
        result, errors = dm.expand_manifest(input, opened_files)

        expected = [
            Entry(
                destination='bin/foo_renamed',
                source='some-variant/foo',
                label='//src/foo(variant)')
        ]
        self.assertListEqual(result, expected)
        self.assertEqual(opened_files, set())
        self.assertFalse(errors)


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
