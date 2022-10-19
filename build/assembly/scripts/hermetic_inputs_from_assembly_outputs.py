#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys
import os

from assembly import PackageManifest
from depfile import DepFile
from serialization import json_load


def main():
    parser = argparse.ArgumentParser(
        description=
        'Generate a hermetic inputs file that includes the outputs of Assembly')
    parser.add_argument(
        '--partitions',
        type=argparse.FileType('r'),
        required=True,
        help=
        'The partitions config that follows this schema: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/src/developer/ffx/plugins/assembly/#partitions-config'
    )
    parser.add_argument(
        '--output',
        type=argparse.FileType('w'),
        required=True,
        help='The location to write the hermetic inputs file')
    # TODO(fxbug.dev/110940): Avoid including transitive dependencies (blobs).
    parser.add_argument(
        '--include-blobs',
        action='store_true',
        help='Whether to include packages and blobs')
    parser.add_argument(
        '--system',
        type=argparse.FileType('r'),
        nargs='*',
        help=
        'A list of system image manifests that follow this schema: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/src/developer/ffx/plugins/assembly/#images-manifest'
    )
    parser.add_argument(
        '--depfile',
        type=argparse.FileType('w'),
        help='A depfile listing all the files opened by this script')
    args = parser.parse_args()

    # A list of the implicit inputs.
    inputs = []

    # Add all the bootloaders as inputs.
    partitions = json.load(args.partitions)
    for bootloader in partitions.get('bootloader_partitions', []):
        inputs.append(bootloader['image'])
    for bootstrap in partitions.get('bootstrap_partitions', []):
        inputs.append(bootstrap['image'])
    for credential in partitions.get('unlock_credentials', []):
        inputs.append(credential)

    # Add all the system images as inputs.
    manifests = []
    for manifest_file in args.system:
        manifest = json.load(manifest_file)
        for image in manifest:
            inputs.append(image['path'])
            if not args.include_blobs:
                continue

            # If we must include blobs, then collect the package manifests from
            # the blobfs image.
            if image['type'] == 'blk' and image['name'] == 'blob':
                packages = []
                packages.extend(image['contents']['packages'].get('base', []))
                packages.extend(image['contents']['packages'].get('cache', []))
                manifests.extend([package['manifest'] for package in packages])

    # If we collected any package manifests, include all the blobs referenced
    # by them.
    for manifest_path in manifests:
        inputs.append(manifest_path)
        with open(manifest_path, 'r') as f:
            package_manifest = json_load(PackageManifest, f)
            for blob in package_manifest.blobs:
                blob_source = blob.source_path
                if package_manifest.blob_sources_relative:
                    blob_source = os.path.join(
                        os.path.dirname(manifest_path), blob_source)
                inputs.append(blob_source)

    # Write the hermetic inputs file.
    args.output.writelines(f"{input}\n" for input in sorted(inputs))

    # Write the depfile.
    if args.depfile:
        DepFile.from_deps(args.output.name, manifests).write_to(args.depfile)


if __name__ == '__main__':
    sys.exit(main())
