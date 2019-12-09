#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import sys

FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # build
    os.path.dirname(             # dart
    os.path.abspath(__file__))))

sys.path += [os.path.join(FUCHSIA_ROOT, 'third_party', 'pyyaml', 'lib')]
import yaml


def main():
    parser = argparse.ArgumentParser(
        'Verifies that all .dart files are included in sources')
    parser.add_argument(
        '--package_root',
        help='Path to the directory hosting the library',
        required=True)
    parser.add_argument(
        '--source_dir',
        help='Path to the directory containing the package sources',
        required=True)
    parser.add_argument(
        '--stamp',
        help='File to touch when source checking succeeds',
        required=True)
    parser.add_argument(
        'sources', help='source files', nargs=argparse.REMAINDER)
    args = parser.parse_args()

    if "third_party" in args.package_root:
        with open(args.stamp, 'w') as stamp:
            stamp.write('Success!')
            return 0

    source_files = set(args.sources)
    source_root = os.path.join(args.package_root, args.source_dir)
    missing_sources = []
    exclude_dirs = ["testing"]
    slice_length = len(args.package_root) + len(args.source_dir) + 2
    for (dirpath, dirnames, filenames) in os.walk(source_root, topdown=True):
        dirnames[:] = [d for d in dirnames if d not in exclude_dirs]
        for filename in filenames:
            full_filename = os.path.join(dirpath[slice_length:], filename)
            [_, file_extension] = os.path.splitext(filename)
            if file_extension == '.dart' and full_filename not in source_files:
                missing_sources.extend([full_filename])

    # We found one or more source files in the directory that was not included in sources.
    if missing_sources:
        print(
            'Source files found that were missing from the "sources" parameter:'
        )
        for source in missing_sources:
            print('"%s",' % source)
        return 1
    with open(args.stamp, 'w') as stamp:
        stamp.write('Success!')


if __name__ == '__main__':
    sys.exit(main())
