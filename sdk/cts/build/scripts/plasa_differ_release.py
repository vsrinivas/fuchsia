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
import shutil
from dataclasses import dataclass
from collections import defaultdict
from typing import List
from plasa_differ import *


# This class is executed as a test when run in a CTS release
# (e.g. //prebuilt/cts/...).
class VerifyPlasaDiffer(unittest.TestCase):

    def test_cts_release_e2e(self):
        try:
            main()
        except Exception as e:
            print(e)
            # TODO(fxbug.dev/92170): Make this a blocking test once the FIDL
            # versioning strategy has been implemented.
            pass


# Copy all necessary artifacts to the staging area (//out/default/cts/...),
# so they can be packaged into the CTS archive.
def prepare_release():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--plasa_manifest',
        help='Path to the PlaSA manifest file in the CTS release.',
        required=True)
    parser.add_argument(
        '--plasa_root',
        help=
        'Path to the root directory for all plasa files in the CTS archive.',
        required=True)
    parser.add_argument(
        '--fragments_root',
        help='Path to the root directory for fragment files in the CTS archive.',
        required=True)
    parser.add_argument(
        '--build_root', help='Path to the root build directory.', required=True)
    parser.add_argument(
        '--depfile', help='Path to the dependencies file.', required=True)
    args = parser.parse_args()
    depfile_dict = defaultdict(list)
    manifest_relpath = ''

    # Create PlaSA root directory and fragment subdirectories in the
    # cts archive staging area.
    for kind in ['', 'fidl']:
        try:
            dir_path = os.path.join(args.fragments_root, kind)
            dir_relpath = os.path.relpath(dir_path, args.build_root)
            manifest_relpath = os.path.relpath(
                args.plasa_manifest, args.build_root)
            depfile_dict[dir_relpath].append(manifest_relpath)

            os.makedirs(os.path.join(args.fragments_root, kind))
        except FileExistsError:
            pass

    # Copy the manifest.plasa.json file to the staging area.
    manifest_filename = os.path.basename(args.plasa_manifest)
    dest_manifest = os.path.join(args.plasa_root, manifest_filename)
    try:
        os.remove(dest_manifest)
    except OSError:
        pass
    shutil.copyfile(args.plasa_manifest, dest_manifest)

    # See fuchsia.dev for information on the format of the manifest file.
    # https://fuchsia.dev/fuchsia-src/development/testing/cts/plasa_manifest?hl=en#manifest_file_format
    with open(args.plasa_manifest, 'r') as manifest:
        data = json.load(manifest)
        for entry in data:
            orig = entry['path']
            dest = os.path.join(args.fragments_root, entry['dest'])
            try:
                os.remove(dest)
            except OSError:
                pass
            shutil.copyfile(orig, dest)

            orig_relpath = os.path.relpath(orig, args.build_root)
            dest_relpath = os.path.relpath(dest, args.build_root)
            depfile_dict[dest_relpath].append(orig_relpath)
            depfile_dict[dest_relpath].append(manifest_relpath)

    with open(args.depfile, 'w') as depfile:
        for k in depfile_dict:
            depfile.write('{}: {}\n'.format(k, ' '.join(depfile_dict[k])))


if __name__ == "__main__":
    if os.getenv("TEST") is not None:
        unittest.main()
    else:
        sys.exit(prepare_release())
