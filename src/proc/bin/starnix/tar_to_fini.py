#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import tarfile
import shutil
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--tar-file', type=argparse.FileType('rb'), required=True)
parser.add_argument('--unpack-dir', required=True)
parser.add_argument(
    '--manifest-file', type=argparse.FileType('w'), required=True)
parser.add_argument('--package-subdir', required=True)
parser.add_argument('--dep-file', type=argparse.FileType('w'), required=True)
args = parser.parse_args()

manifest = {}

with tarfile.open(fileobj=args.tar_file) as tar:
    for entry in tar:
        # an attempt to sanitize the paths
        path = os.path.normpath(entry.name)
        assert not path.startswith('/') and not path.startswith('../')

        extract_dest = os.path.join(args.unpack_dir, path)
        if entry.isreg():
            os.makedirs(os.path.dirname(extract_dest), exist_ok=True)
            with tar.extractfile(entry) as src:
                with open(extract_dest, 'wb') as dst:
                    shutil.copyfileobj(src, dst)
            package_path = path
            if args.package_subdir:
                package_path = args.package_subdir + '/' + package_path
            manifest[package_path] = extract_dest
        elif entry.isdir():
            os.makedirs(extract_dest, exist_ok=True)
        else:
            continue

for k, v in manifest.items():
    args.manifest_file.write(f'{k}={v}\n')
args.dep_file.write(
    f'{" ".join(set(manifest.values()))}: {args.tar_file.name}\n')
