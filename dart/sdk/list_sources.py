#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys


def main():
    parser = argparse.ArgumentParser('Builds a file listing SDK sources')
    parser.add_argument('--out',
                        help='Path to the output file',
                        required=True)
    parser.add_argument('--source-list',
                        help='Path to the list of source files',
                        required=True)
    parser.add_argument('--source-dir',
                        help='Path to the source directory',
                        required=True)
    args = parser.parse_args()

    with open(args.source_list, 'r') as source_file:
        sources = [l.strip() for l in source_file.readlines()]

    with open(args.out, 'w') as out_file:
        for source in sources:
            relative_source = os.path.relpath(source, args.source_dir)
            out_file.write('internal:lib/%s=%s\n' % (relative_source, source))

    return 0


if __name__ == '__main__':
    sys.exit(main())
