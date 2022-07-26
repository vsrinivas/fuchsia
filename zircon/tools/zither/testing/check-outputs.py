#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys

# Cross-references an output manifest with a set of expected filepaths.


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--stamp',
        help='Path to a stamp file to emit on success',
        required=True)
    parser.add_argument(
        '--manifest',
        help='Manifest of output filepaths in the form of a JSON list',
        required=True)
    parser.add_argument(
        'files',
        nargs='*',
        help=
        'Filepaths to cross-reference with the output manifest, passed as positional arguments'
    )
    args = parser.parse_args()

    with open(args.manifest) as manifest_file:
        manifest = set(json.load(manifest_file))
    files = set(args.files)

    diff1 = manifest.difference(files)
    for f in diff1:
        print(
            'Error: \"%s\" appears in manifest, but was not supplied\n' % f,
            flush=True)

    diff2 = files.difference(manifest)
    for f in diff2:
        print(
            'Error: \"%s\" does not appear in manifest, but was supplied\n' % f,
            flush=True)

    if len(diff1) != 0 or len(diff2) != 0:
        return 1

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Success\n')

    return 0


if __name__ == '__main__':
    sys.exit(main())
