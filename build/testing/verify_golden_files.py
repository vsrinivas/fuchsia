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

# Verifies that the candidate golden file matches the provided golden.


def print_failure_msg(golden, candidate, label):
    print(
        f"""
Please acknowledge this change by updating the golden as follows:
```
fx build {candidate} &&
fx run-in-build-dir cp \\
    {candidate} \\
    {golden}
```
Or you can rebuild with `update_goldens=true` in your GN args and {label} in your build graph.

""")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--label', help='GN label for this test', required=True)
    parser.add_argument(
        '--source-root', help='Path to the Fuchsia source root', required=True)
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
            current_comparison_failed = not filecmp.cmp(
                candidate, formatted_golden or golden)
        else:
            current_comparison_failed = True

        if current_comparison_failed:
            any_comparison_failed = True
            type = 'Warning' if args.warn or args.bless else 'Error'
            # Print the source-relative golden so that it can be conveniently
            # navigated to (e.g., in an IDE).
            src_rel_golden = os.path.relpath(
                os.path.join(os.getcwd(), golden), args.source_root)
            print(f'{type}: Golden file mismatch: {src_rel_golden}')

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
