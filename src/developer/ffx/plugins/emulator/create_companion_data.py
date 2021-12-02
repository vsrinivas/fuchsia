#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--binary',
        required=True,
        help=(
            'The path to the binary in the base directory to list files for. This file'
            'is not included in the output'))
    parser.add_argument(
        '--dest_root', required=True, help="destination path root.")
    parser.add_argument(
        '--output', required=True, help='The path to the output file.')
    parser.add_argument(
        '--meta_out', required=True, help='path to metadata for tool.')
    parser.add_argument(
        '--name', required=True, help='name of host tool in metadata.')

    args = parser.parse_args()

    directory = os.path.dirname(args.binary)
    binary_path = os.path.join(
        args.dest_root, os.path.relpath(args.binary, directory))
    # the main binary should be first in the list.
    dest_files = [binary_path]
    with open(args.output, 'w') as f:
        print(f'{binary_path}={args.binary}', file=f)
        for path, dirs, files in os.walk(os.path.abspath(directory)):
            for filename in files:
                source_filepath = os.path.join(path, filename)
                filepath = os.path.join(
                    args.dest_root, os.path.relpath(source_filepath, directory))
                if binary_path != filepath:
                    dest_files += [filepath]
                    print(f'{filepath}={source_filepath}', file=f)

    metadata = {
        'files': dest_files,
        'name': args.name,
        'root': 'tools',
        'type': 'companion_host_tool'
    }

    with open(args.meta_out, 'w') as f:
        print(json.dumps(metadata, sort_keys=True, indent=2), file=f)


if __name__ == u"__main__":
    sys.exit(main())
