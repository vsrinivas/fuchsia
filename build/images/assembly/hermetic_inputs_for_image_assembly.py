#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Generate a hermetic inputs file for image assembly by reading the image
assembly inputs and generating a list of all the files that are read"""

import argparse
import json
import sys
from typing import List

from depfile import DepFile
from assembly import FileEntry, FilePath, ImageAssemblyConfig, PackageManifest
from serialization import json_load


def files_from_package_set(package_set: List[FilePath]) -> List[FilePath]:
    paths = []
    for manifest in package_set:
        paths.append(manifest)
        with open(manifest, 'r') as file:
            manifest = json_load(PackageManifest, file)
            paths.extend([blob.source_path for blob in manifest.blobs])
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--image-assembly-config',
        type=argparse.FileType('r'),
        required=True,
        help='The path to the image assembly config file')
    parser.add_argument(
        '--board-config',
        type=argparse.FileType('r'),
        help='The path to the image assembly board config file')
    parser.add_argument(
        '--output',
        type=str,
        required=True,
        help='The path to the first output of the image assembly target')
    parser.add_argument(
        '--depfile',
        type=argparse.FileType('w'),
        required=True,
        help='The path to the depfile for this script')

    args = parser.parse_args()

    config = ImageAssemblyConfig.load(args.image_assembly_config)

    # Collect the list of files that are read in this script.
    deps = []
    deps.extend(config.base)
    deps.extend(config.cache)
    deps.extend(config.system)
    if deps:
        dep_file = DepFile(args.output)
        dep_file.update(deps)
        dep_file.write_to(args.depfile)

    # Collect the list of inputs to image assembly.
    inputs = []
    inputs.extend(files_from_package_set(config.base))
    inputs.extend(files_from_package_set(config.cache))
    inputs.extend(files_from_package_set(config.system))
    inputs.extend([entry.source for entry in config.bootfs_files])
    inputs.append(config.kernel.path)

    if args.board_config:
        board_config = json.load(args.board_config)
        if 'vbmeta' in board_config:
            vbmeta = board_config['vbmeta']
            if 'key' in vbmeta:
                inputs.append(vbmeta['key'])
            if 'key_metadata' in vbmeta:
                inputs.append(vbmeta['key_metadata'])
            inputs.extend(vbmeta.get('additional_descriptor_files', []))
        if 'zbi' in board_config:
            zbi = board_config['zbi']
            if 'signing_script' in zbi:
                inputs.append(zbi['signing_script']['tool'])

    with open(args.output, 'w') as f:
        for input in inputs:
            f.write(input + '\n')


if __name__ == '__main__':
    sys.exit(main())
