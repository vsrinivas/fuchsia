#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys


def main():
    parser = argparse.ArgumentParser('Builds a metadata file')
    parser.add_argument(
        '--manifest', help='Path to the manifest file to update', required=True)
    parser.add_argument(
        '--out', help='Path to put the updated manifest file.', required=True)
    parser.add_argument(
        '--cts_version', help='Name of the CTS version', required=False)
    args = parser.parse_args()

    version = ''
    if args.cts_version:
        version = args.cts_version

    with open(args.manifest, 'r') as manifest:
        packages = json.load(manifest)

    if version:
        for package in packages:
            package['package'] += f'_{version}'

    with open(args.out, 'w') as out_file:
        json.dump(packages, out_file)

    return 0


if __name__ == '__main__':
    sys.exit(main())
