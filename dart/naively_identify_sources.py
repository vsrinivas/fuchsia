#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys

# Identifies the sources of a Dart by scanning its source directory.
# Note: this is a temporary workaround until all Dart libraries list their
# public sources.

def main():
    parser = argparse.ArgumentParser(
            description='Identifies the sources of a Dart package')
    parser.add_argument('--source-dir',
                        help='Path to the library\'s source directory',
                        required=True)
    parser.add_argument('--output',
                        help='Path to the output file listing the sources',
                        required=True)
    parser.add_argument('--depfile',
                        help='Path to the depfile to generate',
                        required=True)
    parser.add_argument('--depname',
                        help='Name of the target in the depfile',
                        required=True)
    args = parser.parse_args()

    deps = []
    for dirpath, dirnames, filenames in os.walk(args.source_dir):
        for filename in filenames:
            _, extension = os.path.splitext(filename)
            if extension == '.dart':
                deps.append(os.path.join(dirpath, filename))
    deps.sort()

    with open(args.output, 'w') as output_file:
        for dep in deps:
            output_file.write('%s\n' % dep)

    with open(args.depfile, 'w') as dep_file:
        dep_file.write('%s: %s' % (args.depname, ' '.join(deps)))


if __name__ == '__main__':
    sys.exit(main())
