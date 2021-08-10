#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import filecmp
import json
import os
import sys

# Verifies that the current golden file matches the provided golden.


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--golden', help='Path to the golden file', required=True)
    parser.add_argument(
        '--current', help='Path to the local file', required=True)
    parser.add_argument(
        '--stamp', help='Path to the victory file', required=True)
    parser.add_argument(
        '--warn',
        help='Whether API changes should only cause warnings',
        action='store_true')
    args = parser.parse_args()

    if args.golden:
        if not filecmp.cmp(args.golden, args.current):
            type = 'Warning' if args.warn else 'Error'
            print('%s: Golden file mismatch' % type)
            print('Please acknowledge this change by running:')
            # Use abspath in cp command so it works regardless of current
            # working directory.
            print(
                '  cp ' + os.path.abspath(args.current) + ' ' +
                os.path.abspath(args.golden))
            if not args.warn:
                return 1

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Golden!\n')

    return 0


if __name__ == '__main__':
    sys.exit(main())
