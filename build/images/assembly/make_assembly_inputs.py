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

    # The files to put in the output with source mapped to destination.
    file_mapping = {}

    # The files that are read in this script, and the build system needs to be
    # aware of. This will be written to a depfile.
    depfiles = []

    # Add a file's path to one of the lists, relative to CWD.
    # The destination is the path when placed inside "built/artifacts".
    # If the path is prefixed with ../../, the prefix is removed.
    def add_file(file_path):
        # Absolute paths are not portable out-of-tree, therefore if a file is
        # using an absolute path we throw an error.
        if os.path.isabs(file_path):
            raise Exception("Absolute paths are not allowed", file_path)

        source = os.path.relpath(file_path, os.getcwd())
        prefix = "../../"
        if source.startswith(prefix):
            destination = source[len(prefix):]
        else:
            destination = os.path.join("built/artifacts", source)
        file_mapping[source] = destination

    def add_depfile(file_path):
        depfiles.append(os.path.relpath(file_path, os.getcwd()))

    # Add all the blobs in a package to the list.
    def add_package(package_manifest_path):
        add_file(package_manifest_path)
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

    # Add the board config.
    add_file(args.board_config.name)
    board_config = json.load(args.board_config)
    if "vbmeta" in board_config:
        add_file(board_config["vbmeta"]["key"])
        add_file(board_config["vbmeta"]["key_metadata"])
        if "additional_descriptor_files" in board_config["vbmeta"]:
            for descriptor in board_config["vbmeta"][
                    "additional_descriptor_files"]:
                add_file(descriptor)
    if "zbi" in board_config:
        if "signing_script" in board_config["zbi"]:
            add_file(board_config["zbi"]["signing_script"]["tool"])

    # Convert the map into a list of maps.
    files = []
    for src, dest in file_mapping.items():
        files.append({
            "source": src,
            "destination": dest,
        })

    # Write the list.
    json.dump(files, args.output, indent=2)

    # Write the depfile.
    args.depfile.write('build.ninja: ' + ' '.join(depfiles))


if __name__ == '__main__':
    sys.exit(main())
