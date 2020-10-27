#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from tempfile import TemporaryDirectory
import os
import unittest

from verify_cts_deps import VerifyCtsDeps


class CtsVerifyDepsTests(unittest.TestCase):

    def test_init(self):
        root_build_dir = os.getcwd()
        cts_file = 'test.this_is_cts'
        invoker_label = "//sdk/cts/build:verify_cts_deps_test"
        deps = ['//zircon/public/lib/zxtest:zxtest']
        allowed_cts_dirs = ['//sdk/*']

        try:
            VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, deps,
                allowed_cts_dirs)
        except Exception as e:
            self.assertTrue(False, e)

        with self.assertRaises(ValueError):
            VerifyCtsDeps(
                '', cts_file, invoker_label, deps, deps, allowed_cts_dirs)
        with self.assertRaises(ValueError):
            VerifyCtsDeps(
                '/this/path/doesnt/exist', cts_file, invoker_label, deps, deps,
                allowed_cts_dirs)
        with self.assertRaises(ValueError):
            VerifyCtsDeps(
                root_build_dir, '', invoker_label, deps, deps, allowed_cts_dirs)
        with self.assertRaises(ValueError):
            VerifyCtsDeps(
                root_build_dir, cts_file, '', deps, deps, allowed_cts_dirs)
        with self.assertRaises(ValueError):
            VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, [], deps,
                allowed_cts_dirs)
        with self.assertRaises(ValueError):
            VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, [],
                allowed_cts_dirs)
        with self.assertRaises(ValueError):
            VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, deps, [])

    def test_get_file_path(self):
        root_build_dir = os.getcwd()
        cts_file = 'test.this_is_cts'
        invoker_label = "//sdk/cts/build:verify_cts_deps_test"
        deps = ['//zircon/public/lib/zxtest:zxtest']
        allowed_cts_dirs = ['//sdk/*']
        cts_element = VerifyCtsDeps(
            root_build_dir, cts_file, invoker_label, deps, deps,
            allowed_cts_dirs)

        dep = '//zircon/public/lib/zxtest:zxtest'
        self.assertEqual(
            root_build_dir + '/cts/zircon/public/lib/zxtest/zxtest.this_is_cts',
            cts_element.get_file_path(dep))

        dep = '//zircon/public/lib/zxtest'
        self.assertEqual(
            root_build_dir + '/cts/zircon/public/lib/zxtest/zxtest.this_is_cts',
            cts_element.get_file_path(dep))

        dep = '//sdk'
        self.assertEqual(
            root_build_dir + '/cts/sdk/sdk.this_is_cts',
            cts_element.get_file_path(dep))

    def test_verify_deps(self):
        root_build_dir = os.getcwd()
        cts_file = 'test.this_is_cts'
        invoker_label = "//sdk/cts/build:verify_cts_deps_test"
        allowed_cts_deps = ['//zircon/public/lib/zxtest:zxtest']
        allowed_cts_dirs = ['//sdk/*']

        cts_element = VerifyCtsDeps(
            root_build_dir, cts_file, invoker_label, allowed_cts_deps,
            allowed_cts_deps, allowed_cts_dirs)
        self.assertListEqual(cts_element.verify_deps(), [])

        deps = ['//this/dep/isnt/allowed/in:cts']
        cts_element = VerifyCtsDeps(
            root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
            allowed_cts_dirs)
        self.assertListEqual(cts_element.verify_deps(), deps)

        deps = [
            '//this/dep/isnt/allowed/in:cts', '//this/dep/isnt/allowed/in:cts2'
        ]
        cts_element = VerifyCtsDeps(
            root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
            allowed_cts_dirs)
        self.assertListEqual(cts_element.verify_deps(), deps)

        deps = ['//sdk/this/is/a/real:target']
        cts_element = VerifyCtsDeps(
            root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
            allowed_cts_dirs)
        self.assertListEqual(cts_element.verify_deps(), [])

        deps = [
            '//sdk/this/is/a/real:target',
            '//zircon/public/lib/zxtest:zxtest',
        ]
        cts_element = VerifyCtsDeps(
            root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
            allowed_cts_dirs)
        self.assertListEqual(cts_element.verify_deps(), [])

    def test_create_cts_dep_file(self):
        root_build_dir = os.getcwd()
        invoker_label = "//sdk/cts/build:verify_cts_deps_test"
        deps = ['//sdk:sdk', '//zircon/public/lib/zxtest:zxtest']
        allowed_cts_deps = ['//zircon/public/lib/zxtest:zxtest']
        allowed_cts_dirs = ['//sdk/*']

        with TemporaryDirectory() as cts_file:
            cts_file += '/create_cts_dep_file.this_is_cts'
            cts_element = VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
                allowed_cts_dirs)
            cts_element.create_cts_dep_file()
            self.assertTrue(os.path.exists(cts_file))
            with open(cts_file) as f:
                lines = [line.strip() for line in f.readlines()]
                self.assertListEqual(deps, lines)

        with TemporaryDirectory() as cts_file:
            cts_file += '/cts/create_cts_dep_file.this_is_cts'
            cts_element = VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
                allowed_cts_dirs)
            cts_element.create_cts_dep_file()
            self.assertTrue(os.path.exists(cts_file))
            with open(cts_file) as f:
                lines = [line.strip() for line in f.readlines()]
                self.assertListEqual(deps, lines)


if __name__ == '__main__':
    unittest.main()
