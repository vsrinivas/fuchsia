#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import filecmp
import sys

# Verifies that two files have matching contents.


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--stamp', help='Path to the victory file', required=True)
    parser.add_argument('first')
    parser.add_argument('second')
    args = parser.parse_args()



    if not filecmp.cmp(args.first, args.second):
        print(f'Error: file contents differ:\n  {args.first}\n  {args.second}')
        return 1

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Match!\n')

    return 0


if __name__ == '__main__':
    sys.exit(main())
