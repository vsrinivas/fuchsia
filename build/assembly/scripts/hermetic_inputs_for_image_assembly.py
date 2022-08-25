#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Generate a hermetic inputs file for image assembly by reading the image
assembly inputs and generating a list of all the files that are read"""

import argparse
import json
import os
import sys
from typing import List

from depfile import DepFile
from assembly import FileEntry, FilePath, ImageAssemblyConfig, PackageManifest
from serialization import json_load


def get_blob_path(relative_path: str, relative_to_file: str) -> str:
    file_parent = os.path.dirname(relative_to_file)
    path = os.path.join(file_parent, relative_path)
    path = os.path.realpath(path)
    path = os.path.relpath(path, os.getcwd())
    return path


def files_from_package_set(package_set: List[FilePath]) -> List[FilePath]:
    paths = []
    for manifest in package_set:
        paths.append(manifest)
        with open(manifest, 'r') as file:
            package_manifest = json_load(PackageManifest, file)
            blob_sources = []
            for blob in package_manifest.blobs:
                path = blob.source_path
                if package_manifest.blob_sources_relative:
                    path = get_blob_path(path, manifest)
                blob_sources.append(path)
            paths.extend(blob_sources)
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--image-assembly-config',
        type=argparse.FileType('r'),
        required=True,
        help='The path to the image assembly config file')
    parser.add_argument(
        '--images-config',
        type=argparse.FileType('r'),
        help='The path to the image assembly images config file')
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

    config = ImageAssemblyConfig.json_load(args.image_assembly_config)

    # Collect the list of files that are read in this script.
    deps = []
    deps.extend(config.base)
    deps.extend(config.cache)
    deps.extend(config.system)
    deps.extend(config.bootfs_packages)
    if deps:
        dep_file = DepFile(args.output)
        dep_file.update(deps)
        dep_file.write_to(args.depfile)

    # Collect the list of inputs to image assembly.
    inputs = []
    inputs.extend(files_from_package_set(config.base))
    inputs.extend(files_from_package_set(config.cache))
    inputs.extend(files_from_package_set(config.system))
    inputs.extend(files_from_package_set(config.bootfs_packages))
    inputs.extend([entry.source for entry in config.bootfs_files])
    inputs.append(config.kernel.path)

    if args.images_config:
        images_config = json.load(args.images_config)['images']
        for image in images_config:
            if image['type'] == 'vbmeta':
                if 'key' in image:
                    inputs.append(image['key'])
                if 'key_metadata' in image:
                    inputs.append(image['key_metadata'])
                inputs.extend(image.get('additional_descriptor_files', []))
            elif image['type'] == 'zbi':
                if 'postprocessing_script' in image:
                    script = image['postprocessing_script']
                    if 'path' in script:
                        inputs.append(script['path'])

    with open(args.output, 'w') as f:
        for input in inputs:
            f.write(input + '\n')


if __name__ == '__main__':
    sys.exit(main())
