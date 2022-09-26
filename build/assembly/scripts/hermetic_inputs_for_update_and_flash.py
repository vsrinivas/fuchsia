#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys


def main():
    parser = argparse.ArgumentParser(
        description=
        'Generate a hermetic inputs file for ffx assembly create-update')
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
    parser.add_argument(
        '--system',
        type=argparse.FileType('r'),
        nargs='*',
        help=
        'A list of system image manifests that follow this schema: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/src/developer/ffx/plugins/assembly/#images-manifest'
    )
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
    for manifest_file in args.system:
        manifest = json.load(manifest_file)
        inputs.extend([image['path'] for image in manifest])

    # Write the hermetic inputs file.
    args.output.writelines(f"{input}\n" for input in sorted(inputs))


if __name__ == '__main__':
    sys.exit(main())
