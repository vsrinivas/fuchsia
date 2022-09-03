#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import filecmp
import json
import os
import re
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


def compare(candidate, golden, ignore_space_change):
    if ignore_space_change:
        with open(candidate, 'r') as candidate:
            with open(golden, 'r') as golden:
                candidate = candidate.readlines()
                golden = golden.readlines()
                if candidate == golden:
                    return True
                normalize_spaces = lambda lines: [
                    re.sub(r'\s+', ' ', line) for line in lines
                ]
                candidate = normalize_spaces(candidate)
                golden = normalize_spaces(golden)
                return candidate == golden
    return filecmp.cmp(candidate, golden)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--label', help='GN label for this test', required=True)
    parser.add_argument(
        '--comparisons',
        metavar='FILE',
        help='Path at which to find the JSON file containing the comparisons',
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
    parser.add_argument(
        '--ignore-space-change',
        help='Whether to ignore changes in the amount of white space',
        action='store_true')
    args = parser.parse_args()

    with open(args.comparisons) as f:
        comparisons = json.load(f)

    any_comparison_failed = False
    inputs = []
    for comparison in comparisons:
        candidate = comparison["candidate"]
        golden = comparison["golden"]
        inputs.extend([candidate, golden])

        # A formatted golden might have been supplied. Compare against that if
        # present. (In the case of a non-existent golden, this file is empty.)
        formatted_golden = comparison.get("formatted_golden")
        if formatted_golden:
            inputs.append(formatted_golden)

        if os.path.exists(golden):
            current_comparison_failed = not compare(
                candidate, formatted_golden or golden, args.ignore_space_change)
        else:
            current_comparison_failed = True

        if current_comparison_failed:
            any_comparison_failed = True
            type = 'Warning' if args.warn or args.bless else 'Error'
            print('%s: Golden file mismatch' % type)

            if args.bless:
                os.makedirs(os.path.dirname(golden), exist_ok=True)
                shutil.copyfile(candidate, golden)
            else:
                print_failure_msg(golden, candidate, args.label)

    if any_comparison_failed and not args.bless and not args.warn:
        return 1

    with open(args.stamp_file, 'w') as stamp_file:
        stamp_file.write('Golden!\n')

    with open(args.depfile, 'w') as depfile:
        depfile.write('%s: %s\n' % (args.stamp_file, ' '.join(inputs)))

    return 0


if __name__ == '__main__':
    sys.exit(main())
