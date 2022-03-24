#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import unittest
import os
import sys
import json
import subprocess

from tempfile import TemporaryDirectory
from dataclasses import dataclass
from typing import List

from plasa_differ import *

parser = argparse.ArgumentParser()
parser.add_argument(
    '--fragments_dir',
    help='Path to dir holding all example fragment files.',
    required=True)
parser.add_argument(
    '--utils_dir',
    help='Path to directory holding all of the diffing tools.',
    required=True)
ARGS = parser.parse_args()

# The python_host_test build rule calls `unittest.main`.
# So we need to get rid of the test arguments in order
# to prevent them from interfering with `unittest`'s args.

# Pop twice for each flag to get rid of their flags and values.
# Note: The next line only works because all args are marked as "required".
for i in range(2 * len(vars(ARGS))):
    sys.argv.pop()


class VerifyPlasaDiff(unittest.TestCase):

    def create_default_plasa_differ(
            self,
            manifest,
            kinds=['api_fidl', 'api_cpp'],
            utils_dir=ARGS.utils_dir):
        return PlasaDiffer(
            before_manifest=manifest,
            after_manifest=manifest,
            kinds=kinds,
            utils_dir=utils_dir,
        )

    def create_plasa_fragment(self, root, path, kind, write_to_disk=False):
        fragment = {
            'file': '//out/default/currently/unused',
            'kind': kind,
            'dest': 'tot/path/currently/unused',
            'path': path,
        }
        if write_to_disk:
            with open(os.path.join(root, fragment['path']), 'w') as f:
                f.write("summary")
        return fragment

    def create_plasa_manifest(
            self,
            root,
            name="plasa.manifest.json",
            num_fidl=0,
            num_cpp=0,
            create_fragments=True):

        fragments = []
        for i in range(num_fidl):
            name = 'fidl.plasa.diff{}.test.api_summary.json'.format(i)
            fragment = self.create_plasa_fragment(
                root, name, 'api_fidl', create_fragments)
            fragments.append(fragment)

        for i in range(num_cpp):
            name = 'cpp.plasa.diff{}.test.api_summary.json'.format(i)
            fragment = self.create_plasa_fragment(
                root, name, 'api_cpp', create_fragments)
            fragments.append(fragment)

        plasa_manifest = os.path.join(root, name)
        with open(plasa_manifest, 'w') as f:
            json.dump(fragments, f)

        return plasa_manifest

    def test_init(self):
        """Verify we can initialize the PlasaDiffer class successfully.
        """
        kinds = ["api_fidl"]

        with TemporaryDirectory() as root_build_dir:
            manifest = self.create_plasa_manifest(root_build_dir)
            try:
                return PlasaDiffer(
                    before_manifest=manifest,
                    after_manifest=manifest,
                    kinds=kinds,
                    utils_dir=ARGS.utils_dir,
                )
            except Exception as e:
                self.assertTrue(False, e)

            with self.assertRaises(ValueError):
                return PlasaDiffer(
                    before_manifest=manifest,
                    after_manifest=manifest,
                    kinds=None, # not allowed
                    utils_dir=ARGS.utils_dir,
                )

            with self.assertRaises(ValueError):
                return PlasaDiffer(
                    before_manifest=manifest,
                    after_manifest=manifest,
                    kinds=None,
                    utils_dir='/does/not/exist',  # not allowed
                )

    def test_load_manifest(self):
        """Verify we can successfully load a PlaSA manifest file into memory.
        """

        # Ensure no fragments are found in an empty PlaSA manifest.
        with TemporaryDirectory() as root_build_dir:
            manifest = self.create_plasa_manifest(
                root_build_dir, num_fidl=0, num_cpp=0)
            pd = self.create_default_plasa_differ(manifest, kinds=['api_fidl'])
            result = pd.load_manifest(manifest)
            self.assertTrue(len(result) == 0)

        # Ensure fragments can be successfully identified in a PlaSA manifest.
        with TemporaryDirectory() as root_build_dir:
            manifest = self.create_plasa_manifest(
                root_build_dir, num_fidl=5, num_cpp=0)
            pd = self.create_default_plasa_differ(manifest, kinds=['api_fidl'])
            result = pd.load_manifest(manifest)
            self.assertTrue(len(result) == 5)

        # Ensure fragments of a different "kind" are skipped by PlasaDiffer.
        with TemporaryDirectory() as root_build_dir:
            manifest = self.create_plasa_manifest(
                root_build_dir, num_fidl=0, num_cpp=5)
            pd = self.create_default_plasa_differ(manifest, kinds=['api_fidl'])
            result = pd.load_manifest(manifest)
            self.assertTrue(len(result) == 0)

        # Ensure PlasaDiffer can diff multiple "kinds" in the same run.
        with TemporaryDirectory() as root_build_dir:
            manifest = self.create_plasa_manifest(
                root_build_dir, num_fidl=5, num_cpp=5)
            pd = self.create_default_plasa_differ(
                manifest, kinds=['api_fidl', 'api_cpp'])
            result = pd.load_manifest(manifest)
            self.assertTrue(len(result) == 10)

        # PlasaDiffer must raise an exception if a fragment file doesn't exist.
        with TemporaryDirectory() as root_build_dir:
            manifest = self.create_plasa_manifest(
                root_build_dir, num_fidl=5, num_cpp=5, create_fragments=False)
            pd = self.create_default_plasa_differ(
                manifest, kinds=['api_fidl', 'api_cpp'])
            with self.assertRaises(ValueError):
                pd.load_manifest(manifest)

    def test_diff_fragment(self):
        """Verify we are able to diff two fragments against each other.
        """

        compatible_addition = [{'kind': 'table', 'name': 'zzzfake_name'}]

        with TemporaryDirectory() as root_build_dir:
            try:
                manifest = self.create_plasa_manifest(root_build_dir)
                pd = self.create_default_plasa_differ(manifest)
            except Exception as e:
                self.assertTrue(False, e)

            original_path = os.path.join(root_build_dir, "original.json")
            compatible_path = os.path.join(root_build_dir, "compatible.json")
            incompatible_path = os.path.join(
                root_build_dir, "incompatible.json")
            example_fidl_summary = os.path.join(ARGS.fragments_dir, "fidl.json")
            with open(example_fidl_summary, 'r') as example,\
                    open(original_path, 'w') as original,\
                    open(compatible_path, 'w') as compatible,\
                    open(incompatible_path, 'w') as incompatible:

                data = json.load(example)
                json.dump(data, original)

                data.extend(compatible_addition)
                json.dump(data, compatible)

                data.pop(1)
                json.dump(data, incompatible)

            original_fragment = Fragment("", "api_fidl", "", "", original_path)
            compatible_fragment = Fragment(
                "", "api_fidl", "", "", compatible_path)
            incompatible_fragment = Fragment(
                "", "api_fidl", "", "", incompatible_path)

            # Ensure we can successfully diff the same file.
            message, is_compatible = pd.diff_fragments(
                original_fragment, original_fragment)
            self.assertTrue(is_compatible, msg=message)

            # Ensure we can successfully diff compatible files.
            message, is_compatible = pd.diff_fragments(
                original_fragment, compatible_fragment)
            self.assertTrue(is_compatible, msg=message)

            # Ensure we can successfully detect incompatible API changes.
            message, is_compatible = pd.diff_fragments(
                original_fragment, incompatible_fragment)
            self.assertFalse(is_compatible, msg=message)


if __name__ == "__main__":
    unittest.main()
