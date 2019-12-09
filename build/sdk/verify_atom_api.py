#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import filecmp
import json
import sys

# Verifies if the API for an atom has changed.
# This is done with a simple file comparison of the API file version.


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--reference', help='Path to the golden API file', required=True)
    parser.add_argument(
        '--current', help='Path to the local API file', required=True)
    parser.add_argument(
        '--stamp', help='Path to the victory file', required=True)
    parser.add_argument(
        '--warn',
        help='Whether API changes should only cause warnings',
        action='store_true')
    args = parser.parse_args()

    if args.reference:
        if not filecmp.cmp(args.reference, args.current):
            type = 'Warning' if args.warn else 'Error'
            print('%s: API has changed!' % type)
            print('Please acknowledge this change by running:')
            print('  cp ' + args.current + ' ' + args.reference)
            if not args.warn:
                return 1

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('API is good!\n')

    return 0


if __name__ == '__main__':
    sys.exit(main())
