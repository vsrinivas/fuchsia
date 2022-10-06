#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import pathlib
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--cmc-path',
        help='Path to cmc binary.',
        type=pathlib.Path,
        required=True)
    parser.add_argument(
        '--stamp',
        help='Path to stamp file to write.',
        type=pathlib.Path,
        required=True)
    parser.add_argument(
        '--manifest-metadata',
        help='Path to JSON file with a length-1 list of component manifests.',
        type=argparse.FileType('r'),
        required=True)
    parser.add_argument(
        '--fromfile',
        help='Path to list of expected includes.',
        type=pathlib.Path,
        required=True)
    parser.add_argument(
        '--depfile',
        help='Path to depfile to write.',
        type=pathlib.Path,
        required=True)
    parser.add_argument(
        '--includeroot',
        help='Path to root of includes.',
        type=pathlib.Path,
        required=True)
    parser.add_argument(
        '--includepath',
        help='Additional path for relative includes.',
        type=pathlib.Path,
        nargs='+')
    args = parser.parse_args()

    with args.manifest_metadata as f:
        manifest_paths = json.load(f)

    if len(manifest_paths) != 1:
        print('Manifest metadata from GN must include exactly 1 source path.')
        return 1
    manifest_path = manifest_paths[0]

    cmc_args = [
        args.cmc_path,
        '--stamp',
        args.stamp,
        'check-includes',
        manifest_path,
        '--fromfile',
        args.fromfile,
        '--depfile',
        args.depfile,
        '--includeroot',
        args.includeroot,
    ]

    for p in args.includepath:
        cmc_args += ['--includepath', p]

    return subprocess.run(cmc_args).returncode


if __name__ == '__main__':
    sys.exit(main())
