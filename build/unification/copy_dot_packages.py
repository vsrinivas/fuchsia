#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys

# Copies a .packages file from one location to another, updating relative paths
# in the file in the process.


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--source', help='Path to the original .packages file', required=True)
    parser.add_argument(
        '--destination', help='Path to the new .packages file', required=True)
    args = parser.parse_args()

    source_base = os.path.dirname(args.source)
    dest_base = os.path.dirname(args.destination)

    with open(args.source, 'r') as source_file:
        with open(args.destination, 'w') as dest_file:
            for _, line in enumerate(source_file):
                name, location = line.strip().split(':', 1)
                new_location = os.path.relpath(
                    os.path.join(source_base, location), dest_base)
                dest_file.write('%s:%s\n' % (name, new_location))


if __name__ == '__main__':
    sys.exit(main())
