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
parser.add_argument(
    '--out_dir',
    help='Path to a directory for saving output diff files.',
    required=True)
ARGS = parser.parse_args()

# Create the out directory if it doesn't exist.
if not os.path.exists(ARGS.out_dir):
    os.makedirs(ARGS.out_dir)

# The python_host_test build rule calls `unittest.main`.
# So we need to get rid of the test arguments in order
# to prevent them from interfering with `unittest`'s args.

# Pop twice for each flag to get rid of their flags and values.
# Note: The next line only works because all args are marked as "required".
for i in range(2 * len(vars(ARGS))):
    sys.argv.pop()


class VerifyPlasaDiff(unittest.TestCase):

    def create_plasa_manifest(
            self,
            tmp_dir,
            root="",
            name="plasa.manifest.json",
            num_fidl=0,
            num_cpp=0,
            create_fragments=True):

        root = os.path.join(tmp_dir, 'cts/plasa')
        (not os.path.exists(root)) and os.makedirs(root)

        fragment_root = os.path.join(root, 'fragments')
        os.makedirs(fragment_root)
        os.makedirs(os.path.join(fragment_root, "fidl"))
        os.makedirs(os.path.join(fragment_root, "cpp"))

        def create_fragments_helper(root, count, kind, create_fragments):
            content = []
            for i in range(count):
                entry = {
                    'file':
                        '//out/default/currently/unused',
                    'kind':
                        kind,
                    'path':
                        'tot/path/currently/unused',
                    'dest':
                        '{}/plasa.diff{}.test.api_summary.json'.format(
                            kind.replace('api_', ''), i),
                }
                content.append(entry)
                if create_fragments:
                    fragment_path = os.path.join(root, entry['dest'])
                    with open(fragment_path, 'w') as f:
                        f.write("summary")
            return content

        content = []
        if (num_fidl > 0):
            content.extend(
                create_fragments_helper(
                    fragment_root, num_fidl, 'api_fidl', create_fragments))
        if (num_cpp > 0):
            content.extend(
                create_fragments_helper(
                    fragment_root, num_cpp, 'api_cpp', create_fragments))

        plasa_manifest = os.path.join(root, name)
        with open(plasa_manifest, 'w') as f:
            json.dump(content, f)

        return {
            'left_manifest': plasa_manifest,
            'left_fragments_root': fragment_root,
            'right_manifest': plasa_manifest,
            'right_fragments_root': fragment_root,
            'kinds': ['api_fidl', 'api_cpp'],
            'utils_dir': ARGS.utils_dir,
            'out_dir': ARGS.out_dir
        }

    def test_init(self):
        """Verify we can initialize the PlasaDiffer class successfully.
        """
        kinds = ["api_fidl"]

        with TemporaryDirectory() as root_build_dir:
            temp_files = self.create_plasa_manifest(root_build_dir)
            args = list(temp_files.values())
            try:
                pd = PlasaDiffer(**temp_files)
            except Exception as e:
                self.assertTrue(False, e)

            with self.assertRaises(ValueError):
                PlasaDiffer(
                    "/does/not/exist", args[1], args[2], args[3], args[4],
                    args[5], args[6])
            with self.assertRaises(ValueError):
                PlasaDiffer(
                    args[0], "/does/not/exist", args[2], args[3], args[4],
                    args[5], args[6])
            with self.assertRaises(ValueError):
                PlasaDiffer(
                    args[0], args[1], "/does/not/exist", args[3], args[4],
                    args[5], args[6])
            with self.assertRaises(ValueError):
                PlasaDiffer(
                    args[0], args[1], args[2], "/does/not/exist", args[4],
                    args[5], args[6])
            with self.assertRaises(ValueError):
                PlasaDiffer(
                    args[0], args[1], args[2], args[3], [], args[5], args[6])
            with self.assertRaises(ValueError):
                PlasaDiffer(
                    args[0], args[1], args[2], args[3], args[4],
                    "/does/not/exist", args[6])

    def test_load_manifest(self):
        """Verify we can successfully load a PlaSA manifest file into memory.
        """

        def modified_manifest(
                num_fidl, num_cpp, kinds=['api_fidl'], create_fragments=True):
            with TemporaryDirectory() as root_build_dir:
                #try:
                temp_files = self.create_plasa_manifest(
                    root_build_dir,
                    num_fidl=num_fidl,
                    num_cpp=num_cpp,
                    create_fragments=create_fragments)
                args = list(temp_files.values())
                args[4] = kinds
                pd = PlasaDiffer(*args)
                result = pd.load_manifest(
                    temp_files['left_manifest'],
                    temp_files['left_fragments_root'])
                return result
                #except Exception as e:
                #    self.assertTrue(False, e)

        # Ensure no fragments are found in an empty PlaSA manifest.
        result = modified_manifest(0, 0)
        self.assertTrue(len(result) == 0)

        # Ensure fragments can be successfully identified in a PlaSA manifest.
        result = modified_manifest(5, 0)
        self.assertTrue(len(result) == 5)

        # Ensure fragments of a different "kind" are skipped by PlasaDiffer.
        result = modified_manifest(0, 5)
        self.assertTrue(len(result) == 0)

        # Ensure PlasaDiffer can diff multiple "kinds" in the same run.
        result = modified_manifest(5, 5, kinds=['api_fidl', 'api_cpp'])
        self.assertTrue(len(result) == 10)

        # PlasaDiffer must raise an exception if a fragment file doesn't exist.
        with self.assertRaises(ValueError):
            modified_manifest(
                5, 5, kinds=['api_fidl', 'api_cpp'], create_fragments=False)

    def test_diff_fragment(self):
        """Verify we are able to diff two fragments against each other.
        """

        compatible_addition = [{'kind': 'table', 'name': 'zzzfake_name'}]

        with TemporaryDirectory() as root_build_dir:
            try:
                temp_files = self.create_plasa_manifest(root_build_dir)
                args = temp_files.values()
                pd = PlasaDiffer(*args)
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
