#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import filecmp
import json
import os
import shutil
import sys

# Verifies that the current golden file matches the provided golden.


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--label', help='GN label for this test', required=True)
    parser.add_argument(
        '--comparisons',
        metavar='FILE:GOLDEN',
        nargs='+',
        help='A tuple of filepaths to compare, given as FILE:GOLDEN',
        required=True,
    )
    parser.add_argument(
        '--result-file', help='Path to the victory file', required=True)
    parser.add_argument(
        '--bless',
        help="Overwrites current with golden if they don't match.",
        action='store_true')
    parser.add_argument(
        '--warn',
        help='Whether API changes should only cause warnings',
        action='store_true')
    args = parser.parse_args()

    diffs = False
    for comparison in args.comparisons:
        tokens = comparison.split(':')
        if len(tokens) != 2:
            print(
                '--comparison value \"%s\" must be given as \"FILE:GOLDEN\"' %
                comparison)
            return 1
        current, golden = tokens
        if not filecmp.cmp(current, golden):
            diffs = True
            type = 'Warning' if args.warn or args.bless else 'Error'
            print('%s: Golden file mismatch' % type)

            if args.bless:
                shutil.copyfile(current, golden)
            else:
                print(
                    'Please acknowledge this change by updating the golden.\n')
                print('You can run this command:')
                # Use abspath in cp command so it works regardless of current
                # working directory.
                print(
                    '  cp ' + os.path.abspath(current) + ' ' +
                    os.path.abspath(golden))
                print(
                    'Or you can rebuild with `bless_goldens=true` in your GN args and'
                )
                print(f'`{args.label}` in your build graph.')

    if diffs and not args.bless and not args.warn:
        return 1

    with open(args.result_file, 'w') as result_file:
        result_file.write('Golden!\n')

    return 0


if __name__ == '__main__':
    sys.exit(main())
