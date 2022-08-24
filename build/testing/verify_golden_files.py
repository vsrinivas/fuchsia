#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import filecmp
import os
import shutil
import sys

# Verifies that the candidate golden file matches the provided golden.


def print_failure_msg(golden, candidate, label):
    # Use abspath in cp command so it works regardless of the candidate working
    # directory.
    candidate = os.path.abspath(candidate)
    golden = os.path.abspath(golden)
    print(
        f"""
Please acknowledge this change by updating the golden.
You can run this command:
```
cp {candidate} \\
    {golden}
```
Or you can rebuild with `bless_goldens=true` in your GN args and {label} in your build graph.
""")


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
        '--depfile', help='Path at which to write the depfile', required=True)
    parser.add_argument(
        '--stamp-file',
        help='Path at which to write the stamp file',
        required=True)
    parser.add_argument(
        '--bless',
        help=
        "Overwrites the golden with the candidate if they don't match - or creates it if it does not yet exist",
        action='store_true')
    parser.add_argument(
        '--warn',
        help='Whether API changes should only cause warnings',
        action='store_true')
    args = parser.parse_args()

    diffs = False
    inputs = []
    for comparison in args.comparisons:
        tokens = comparison.split(':')
        if len(tokens) != 2:
            print(
                '--comparison value \"%s\" must be given as \"FILE:GOLDEN\"' %
                comparison)
            return 1
        candidate, golden = tokens
        inputs.extend([candidate, golden])

        golden_exists = os.path.exists(golden)
        if not golden_exists and args.bless:
            # Create a blank golden file if one does not yet exist. This is a
            # convenience measure that allows for the subsequent diff to fail
            # gracefully and for the golden to be auto-updated with the desired
            # contents.
            with open(golden, 'w'):
                golden_exists = True

        if not golden_exists or not filecmp.cmp(candidate, golden):
            diffs = True
            type = 'Warning' if args.warn or args.bless else 'Error'
            print('%s: Golden file mismatch' % type)

            if args.bless:
                assert golden_exists  # Should have been created above.
                shutil.copyfile(candidate, golden)
            else:
                print_failure_msg(golden, candidate, args.label)

    if diffs and not args.bless and not args.warn:
        return 1

    with open(args.stamp_file, 'w') as stamp_file:
        stamp_file.write('Golden!\n')

    with open(args.depfile, 'w') as depfile:
        depfile.write('%s: %s\n' % (args.stamp_file, ' '.join(inputs)))

    return 0


if __name__ == '__main__':
    sys.exit(main())
