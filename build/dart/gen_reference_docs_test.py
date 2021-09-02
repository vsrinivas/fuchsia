#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

import gen_reference_docs


class GenReferenceDocsTest(unittest.TestCase):

    def setUp(self):
        # Create a temporary directory
        self.temp_dir = tempfile.mkdtemp()

    def tearDown(self):
        # Remove the directory after the test
        shutil.rmtree(self.temp_dir)

    def test_is_dart_package_dir_does_not_exist(self):
        package_dir = os.path.join(self.temp_dir, 'test_package')
        self.assertFalse(gen_reference_docs.is_dart_package_dir(package_dir))

    def test_is_dart_package_dir_missing_lib(self):
        package_dir = os.path.join(self.temp_dir, 'test_package')
        os.mkdir(package_dir)
        self.assertFalse(gen_reference_docs.is_dart_package_dir(package_dir))

    def test_is_dart_package_dir_missing_pubspec(self):
        package_dir = os.path.join(self.temp_dir, 'test_package')
        lib_dir = os.path.join(package_dir, 'lib')
        os.makedirs(lib_dir)
        self.assertFalse(gen_reference_docs.is_dart_package_dir(package_dir))

    def test_is_dart_package_dir_invalid_pubspec(self):
        package_dir = os.path.join(self.temp_dir, 'test_package')
        lib_dir = os.path.join(package_dir, 'lib')
        os.makedirs(lib_dir)
        with open(os.path.join(package_dir, 'pubspec.yaml'), 'w') as f:
            f.write('name: none\n')
        self.assertFalse(gen_reference_docs.is_dart_package_dir(package_dir))

    def test_is_dart_package_dir(self):
        package_dir = os.path.join(self.temp_dir, 'test_package')
        lib_dir = os.path.join(package_dir, 'lib')
        os.makedirs(lib_dir)
        with open(os.path.join(package_dir, 'pubspec.yaml'), 'w') as f:
            f.write('name: test_package\n')
        self.assertTrue(gen_reference_docs.is_dart_package_dir(package_dir))

    def test_collect_top_level_files(self):
        package_dir = os.path.join(self.temp_dir, 'test_package')
        lib_dir = os.path.join(package_dir, 'lib')
        os.makedirs(lib_dir)
        for name in ('foo.dart', 'bar.dart', 'baz.dart', 'foo.java'):
            with open(os.path.join(lib_dir, name), 'w') as f:
                f.write('// comment')

        result = gen_reference_docs.collect_top_level_files(package_dir)
        self.assertEqual(result, ['bar.dart', 'baz.dart', 'foo.dart'])

    def test_collect_top_level_files_empty(self):
        package_dir = os.path.join(self.temp_dir, 'test_package')
        lib_dir = os.path.join(package_dir, 'lib')
        os.makedirs(lib_dir)

        result = gen_reference_docs.collect_top_level_files(package_dir)
        self.assertEqual(result, [])

    def test_collect_top_level_files_no_dart_files(self):
        package_dir = os.path.join(self.temp_dir, 'test_package')
        lib_dir = os.path.join(package_dir, 'lib')
        os.makedirs(lib_dir)
        for name in ('foo.py', 'foo.yaml'):
            with open(os.path.join(lib_dir, name), 'w') as f:
                f.write('# comment')

        result = gen_reference_docs.collect_top_level_files(package_dir)
        self.assertEqual(result, [])

    def test_compose_pubspec_content(self):
        packages = {
            'foo': '/path/to/foo',
            'bar': '/path/to/bar',
        }
        expected = expected_pubspec_content = """name: Fuchsia
homepage: https://fuchsia.dev/reference/dart
description: API documentation for fuchsia
environment:
  sdk: '>=2.10.0 <3.0.0'
dependencies:
  foo:
    path: /path/to/foo/
  bar:
    path: /path/to/bar/
"""
        result = gen_reference_docs.compose_pubspec_content(packages)
        self.assertEqual(result, expected)

    def test_walk_rmtree(self):
        with mock.patch.object(gen_reference_docs.os, 'walk') as mock_walk:
            with mock.patch.object(gen_reference_docs.os.path,'islink') as mock_islink:
                with mock.patch.object(gen_reference_docs.os,'unlink') as mock_unlink:
                    with mock.patch.object(gen_reference_docs.os, 'rmdir') as mock_rmdir:
                        mock_walk.return_value = [
                        ('/foo', ('bar',), ('baz',)),
                        ('/foo/bar', (), ('spam', 'eggs'))]
                        mock_islink.return_value = True
                        gen_reference_docs.walk_rmtree('fake_dir')
                        mock_unlink.assert_any_call('/foo/baz')
                        mock_unlink.assert_any_call('/foo/bar/spam')
                        mock_unlink.assert_any_call('/foo/bar/eggs')
                        mock_unlink.assert_any_call('/foo/bar')
                        mock_rmdir.assert_any_call('fake_dir')


    def test_compose_imports_content(self):
        imports = {
            'foo': ['foo.dart', 'another_foo.dart'],
            'bar': ['bar.dart'],
        }
        expected = """library Fuchsia;
import 'package:bar/bar.dart';
import 'package:foo/another_foo.dart';
import 'package:foo/foo.dart';
"""
        result = gen_reference_docs.compose_imports_content(imports)
        self.assertEqual(result, expected)

    def test_fabricate_package(self):
        gen_dir = os.path.join(self.temp_dir, 'gen')
        pubspec = 'pubspec file'
        imports = 'imports file'

        gen_reference_docs.fabricate_package(gen_dir, pubspec, imports)

        with open(os.path.join(gen_dir, 'pubspec.yaml'), 'r') as f:
            self.assertEqual(f.read(), pubspec)
        with open(os.path.join(gen_dir, 'lib', 'lib.dart'), 'r') as f:
            self.assertEqual(f.read(), imports)

    @mock.patch.object(subprocess, 'run')
    def test_generate_docs(self, mock_run):
        mock_run.return_value = subprocess.CompletedProcess(
            args='', returncode=0)

        package_dir = os.path.join(self.temp_dir, 'test_package')
        out_dir = os.path.join(self.temp_dir, 'out')
        prebuilts = os.path.join(self.temp_dir, 'prebuilts')
        result = gen_reference_docs.generate_docs(
            package_dir=package_dir,
            out_dir=out_dir,
            dart_prebuilt_dir=prebuilts)
        self.assertEqual(result, 0)
        mock_run.assert_has_calls(
            [
                mock.call(
                    [os.path.join(prebuilts, 'pub'), 'get'],
                    cwd=package_dir,
                    capture_output=True,
                    universal_newlines=True),
                mock.call(
                    [
                        os.path.join(prebuilts, 'dartdoc'),
                        '--no-validate-links', '--auto-include-dependencies',
                        '--no-enhanced-reference-lookup', '--exclude-packages',
                        'Dart,logging', '--output',
                        os.path.join(out_dir, 'docs'), '--format', 'md'
                    ],
                    cwd=package_dir,
                    capture_output=True,
                    universal_newlines=True),
            ])


if __name__ == '__main__':
    unittest.main()
