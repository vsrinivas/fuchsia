#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from tempfile import TemporaryDirectory
import json
import os
import unittest

from verify_cts_deps import VerifyCtsDeps


class CtsVerifyDepsTests(unittest.TestCase):

    def create_empty_sdk_manifest(self, root_build_dir, manifest_name):
        manifest_path = root_build_dir + '/sdk/manifest/'
        if not os.path.exists(manifest_path):
            os.makedirs(manifest_path)
        sdk_manifest = manifest_path + manifest_name
        if not os.path.isfile(sdk_manifest):
            with open(sdk_manifest, 'w') as f:
                f.write("")

        return sdk_manifest

    def test_init(self):
        cts_file = 'test.this_is_cts'
        invoker_label = "//sdk/cts/build:verify_cts_deps_test"
        deps = ['//zircon/public/lib/zxtest:zxtest']
        allowed_cts_dirs = ['//sdk/*']

        with TemporaryDirectory() as root_build_dir:
            sdk_manifests = [
                self.create_empty_sdk_manifest(root_build_dir, "core")
            ]
            try:
                VerifyCtsDeps(
                    root_build_dir, cts_file, invoker_label, deps, deps,
                    allowed_cts_dirs, sdk_manifests)
            except Exception as e:
                self.assertTrue(False, e)

            with self.assertRaises(ValueError):
                VerifyCtsDeps(
                    '', cts_file, invoker_label, deps, deps, allowed_cts_dirs,
                    sdk_manifests)
            with self.assertRaises(ValueError):
                VerifyCtsDeps(
                    '/this/path/doesnt/exist', cts_file, invoker_label, deps,
                    deps, allowed_cts_dirs, sdk_manifests)
            with self.assertRaises(ValueError):
                VerifyCtsDeps(
                    root_build_dir, '', invoker_label, deps, deps,
                    allowed_cts_dirs, sdk_manifests)
            with self.assertRaises(ValueError):
                VerifyCtsDeps(
                    root_build_dir, cts_file, '', deps, deps, allowed_cts_dirs,
                    sdk_manifests)
            with self.assertRaises(ValueError):
                VerifyCtsDeps(
                    root_build_dir, cts_file, invoker_label, [], deps,
                    allowed_cts_dirs, sdk_manifests)
            with self.assertRaises(ValueError):
                VerifyCtsDeps(
                    root_build_dir, cts_file, invoker_label, deps, [],
                    allowed_cts_dirs, sdk_manifests)
            with self.assertRaises(ValueError):
                VerifyCtsDeps(
                    root_build_dir, cts_file, invoker_label, deps, deps, [],
                    sdk_manifests)
            with self.assertRaises(ValueError):
                VerifyCtsDeps(
                    root_build_dir, cts_file, invoker_label, deps, deps,
                    allowed_cts_dirs, ['/this/path/doesnt/exist'])

    def test_get_file_path(self):
        cts_file = 'test.this_is_cts'
        invoker_label = "//sdk/cts/build:verify_cts_deps_test"
        deps = ['//zircon/public/lib/zxtest:zxtest']
        allowed_cts_dirs = ['//sdk/*']
        with TemporaryDirectory() as root_build_dir:
            sdk_manifests = [
                self.create_empty_sdk_manifest(root_build_dir, "core")
            ]
            cts_element = VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, deps,
                allowed_cts_dirs, sdk_manifests)

            dep = '//zircon/public/lib/zxtest:zxtest'
            self.assertEqual(
                root_build_dir +
                '/cts/zircon/public/lib/zxtest/zxtest.this_is_cts',
                cts_element.get_file_path(dep))

            dep = '//zircon/public/lib/zxtest'
            self.assertEqual(
                root_build_dir +
                '/cts/zircon/public/lib/zxtest/zxtest.this_is_cts',
                cts_element.get_file_path(dep))

            dep = '//sdk'
            self.assertEqual(
                root_build_dir + '/cts/sdk/sdk.this_is_cts',
                cts_element.get_file_path(dep))

    def test_verify_dep_in_sdk(self):
        cts_file = 'test.this_is_cts'
        invoker_label = "//sdk/cts/build:verify_cts_deps_test"
        allowed_cts_deps = ['//zircon/public/lib/zxtest:zxtest']
        allowed_cts_dirs = ['//third_party/dart-pkg/pub/*']
        deps = [
            '//sdk/fidl/fuchsia.io',
            '//sdk/lib/fdio:fdio',
            '//sdk/lib/private_atom:private_atom'
        ]

        fdio_atom = {
            "category": "partner",
            "deps": [],
            "files": [],
            "gn-label": "//sdk/lib/fdio:fdio_sdk_manifest(//build/toolchain/fuchsia:x64)",
            "id": "sdk://pkg/fdio",
            "meta": "pkg/fdio/meta.json",
            "type": "cc_prebuilt_library"
        }
        fuchsia_io_atom = {
            "category": "partner",
            "deps": [],
            "files": [],
            "gn-label": "//sdk/fidl/fuchsia.io:fuchsia.io_sdk(//build/fidl:fidling)",
            "id": "sdk://fidl/fuchsia.io",
            "meta": "fidl/fuchsia.io/meta.json",
            "type": "fidl_library"
        }
        fuchsia_git_atom = {
            "category": "internal",
            "deps": [],
            "files": [],
            "gn-label": "//sdk/lib/private_atom:private_atom(//build/toolchain:fuchsia:x64)",
            "id": "sdk://fidl/fuchsia.io",
            "meta": "fidl/fuchsia.io/meta.json",
            "type": "fidl_library"
        }
        manifest = {
            "atoms": [],
            "ids": []
        }

        with TemporaryDirectory() as root_build_dir:
            sdk_manifest = self.create_empty_sdk_manifest(
                root_build_dir, "core")

            cts_element = VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
                allowed_cts_dirs, [sdk_manifest])
            self.assertFalse(
                cts_element.verify_dep_in_sdk('//sdk/lib/fdio:fdio'))

        with TemporaryDirectory() as root_build_dir:
            sdk_manifest = self.create_empty_sdk_manifest(
                root_build_dir, "core")
            cts_element = VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
                allowed_cts_dirs, [sdk_manifest])

            manifest['atoms'].append(fdio_atom)
            with open(sdk_manifest, 'w') as f:
                json.dump(manifest, f)
            self.assertTrue(
                cts_element.verify_dep_in_sdk('//sdk/lib/fdio:fdio'))
            self.assertFalse(
                cts_element.verify_dep_in_sdk('//sdk/fidl/fuchsia.io'))
            self.assertFalse(
                cts_element.verify_dep_in_sdk('//sdk/lib/private_atom'))

            manifest['atoms'].append(fuchsia_io_atom)
            with open(sdk_manifest, 'w') as f:
                json.dump(manifest, f)
            self.assertTrue(
                cts_element.verify_dep_in_sdk('//sdk/lib/fdio:fdio'))
            self.assertTrue(
                cts_element.verify_dep_in_sdk(
                    '//sdk/fidl/fuchsia.io:fuchsia.io'))
            self.assertFalse(
                cts_element.verify_dep_in_sdk('//sdk/lib/private_atom'))

            manifest['atoms'].append(fuchsia_git_atom)
            with open(sdk_manifest, 'w') as f:
                json.dump(manifest, f)
            self.assertTrue(
                cts_element.verify_dep_in_sdk('//sdk/lib/fdio:fdio'))
            self.assertTrue(
                cts_element.verify_dep_in_sdk(
                    '//sdk/fidl/fuchsia.io:fuchsia.io'))
            self.assertFalse(
                cts_element.verify_dep_in_sdk('//sdk/lib/private_atom'))

            manifest['atoms'] = []

        with TemporaryDirectory() as root_build_dir:
            manifest1 = self.create_empty_sdk_manifest(root_build_dir, "core")
            manifest['atoms'].append(fdio_atom)
            with open(manifest1, 'w') as f:
                json.dump(manifest, f)
            manifest['atoms'] = []
            manifest2 = self.create_empty_sdk_manifest(root_build_dir, "core2")
            manifest['atoms'].append(fuchsia_io_atom)
            with open(manifest2, 'w') as f:
                json.dump(manifest, f)

            cts_element = VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
                allowed_cts_dirs, [manifest1, manifest2])

            self.assertTrue(
                cts_element.verify_dep_in_sdk('//sdk/lib/fdio:fdio'))
            self.assertTrue(
                cts_element.verify_dep_in_sdk(
                    '//sdk/fidl/fuchsia.io:fuchsia.io'))
            self.assertFalse(
                cts_element.verify_dep_in_sdk('//sdk/lib/private_atom'))

    def test_verify_deps(self):
        cts_file = 'test.this_is_cts'
        invoker_label = "//sdk/cts/build:verify_cts_deps_test"
        allowed_cts_deps = ['//zircon/public/lib/zxtest:zxtest']
        allowed_cts_dirs = ['//sdk/*']

        with TemporaryDirectory() as root_build_dir:
            sdk_manifests = [
                self.create_empty_sdk_manifest(root_build_dir, "core")
            ]
            cts_element = VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, allowed_cts_deps,
                allowed_cts_deps, allowed_cts_dirs, sdk_manifests)
            self.assertListEqual(cts_element.verify_deps(), [])

            deps = ['//this/dep/isnt/allowed/in:cts']
            cts_element = VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
                allowed_cts_dirs, sdk_manifests)
            self.assertListEqual(cts_element.verify_deps(), deps)

            deps = [
                '//this/dep/isnt/allowed/in:cts',
                '//this/dep/isnt/allowed/in:cts2',
            ]
            cts_element = VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
                allowed_cts_dirs, sdk_manifests)
            self.assertListEqual(cts_element.verify_deps(), deps)

            deps = ['//sdk/this/is/a/real:target']
            cts_element = VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
                allowed_cts_dirs, sdk_manifests)
            self.assertListEqual(cts_element.verify_deps(), [])

            deps = [
                '//sdk/this/is/a/real:target',
                '//zircon/public/lib/zxtest:zxtest',
            ]
            cts_element = VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
                allowed_cts_dirs, sdk_manifests)
            self.assertListEqual(cts_element.verify_deps(), [])

            deps = [
                '//sdk/lib/fdio:fdio',
                '//third_party/dart-pkg/pub/some-dart-pkg',
                '//zircon/public/lib/zxtest:zxtest',
            ]
            manifest = {
                "atoms": [{
                    "category": "partner",
                    "deps": [],
                    "files": [],
                    "gn-label": "//sdk/lib/fdio:fdio_sdk_manifest(//build/toolchain/fuchsia:x64)",
                    "id": "sdk://pkg/fdio",
                    "meta": "pkg/fdio/meta.json",
                    "type": "cc_prebuilt_library"
                }],
                "ids": []
            }
            with open(sdk_manifests[0], 'w') as sdk_manifest:
                json.dump(manifest, sdk_manifest)
            allowed_cts_dirs = ['//third_party/dart-pkg/pub/*']
            cts_element = VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
                allowed_cts_dirs, sdk_manifests)
            self.assertListEqual(cts_element.verify_deps(), [])

    def test_create_cts_dep_file(self):
        invoker_label = "//sdk/cts/build:verify_cts_deps_test"
        deps = ['//sdk:sdk', '//zircon/public/lib/zxtest:zxtest']
        allowed_cts_deps = ['//zircon/public/lib/zxtest:zxtest']
        allowed_cts_dirs = ['//sdk/*']

        with TemporaryDirectory() as root_build_dir:
            cts_file = root_build_dir + '/create_cts_dep_file.this_is_cts'
            sdk_manifests = [
                self.create_empty_sdk_manifest(root_build_dir, "core")
            ]
            cts_element = VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
                allowed_cts_dirs, sdk_manifests)
            cts_element.create_cts_dep_file()
            self.assertTrue(os.path.exists(cts_file))
            with open(cts_file) as f:
                lines = [line.strip() for line in f.readlines()]
                self.assertListEqual(deps, lines)

        with TemporaryDirectory() as root_build_dir:
            cts_file = root_build_dir + '/cts/create_cts_dep_file.this_is_cts'
            sdk_manifests = [
                self.create_empty_sdk_manifest(root_build_dir, "core")
            ]
            cts_element = VerifyCtsDeps(
                root_build_dir, cts_file, invoker_label, deps, allowed_cts_deps,
                allowed_cts_dirs, sdk_manifests)
            cts_element.create_cts_dep_file()
            self.assertTrue(os.path.exists(cts_file))
            with open(cts_file) as f:
                lines = [line.strip() for line in f.readlines()]
                self.assertListEqual(deps, lines)


if __name__ == '__main__':
    unittest.main()
