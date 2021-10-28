#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys


def main():
    parser = argparse.ArgumentParser(
        description=
        'Create a flat list of files included in the images. This is used to inform infrastructure what files to upload'
    )
    parser.add_argument(
        '--product-config', type=argparse.FileType('r'), required=True)
    parser.add_argument(
        '--board-config', type=argparse.FileType('r'), required=True)
    parser.add_argument('--output', type=argparse.FileType('w'), required=True)
    parser.add_argument('--depfile', type=argparse.FileType('w'), required=True)
    args = parser.parse_args()

    # The files to put in the output.
    files = []

    # The files that are read in this script, and the build system needs to be
    # aware of. This will be written to a depfile.
    depfiles = []

    # Add a file's path to one of the lists, relative to CWD.
    def add_file(file_path):
        files.append(os.path.relpath(file_path, os.getcwd()))

    def add_depfile(file_path):
        depfiles.append(os.path.relpath(file_path, os.getcwd()))

    # Add all the blobs in a package to the list.
    def add_package(package_manifest_path):
        add_depfile(package_manifest_path)
        with open(package_manifest_path, 'r') as f:
            package_manifest = json.load(f)
            if "blobs" in package_manifest:
                for blob in package_manifest["blobs"]:
                    add_file(blob["source_path"])

    # Add the product config.
    add_file(args.product_config.name)
    product_config = json.load(args.product_config)
    if "base" in product_config:
        for package_path in product_config["base"]:
            add_package(package_path)
    if "cache" in product_config:
        for package_path in product_config["cache"]:
            add_package(package_path)
    if "system" in product_config:
        for package_path in product_config["system"]:
            add_package(package_path)
    if "kernel" in product_config:
        add_file(product_config["kernel"]["path"])
    if "bootfs_files" in product_config:
        for bootfs_file in product_config["bootfs_files"]:
            add_file(bootfs_file["source"])

    # TODO: Add the board config.

    # Remove duplicates.
    files = list(set(files))

    # Write the list.
    json.dump(files, args.output, indent=2)

    # Write the depfile.
    args.depfile.write('build.ninja: ' + ' '.join(depfiles))


if __name__ == '__main__':
    sys.exit(main())
