#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import filecmp
import os
import re
import shutil
import subprocess
import sys
import tempfile

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
                normalize_spaces = lambda lines: [re.sub(r'\s+', ' ', line) for line in lines]
                candidate = normalize_spaces(candidate)
                golden = normalize_spaces(golden)
                return candidate == golden
    return filecmp.cmp(candidate, golden)


# Formats a given file - preserving the original contents - and returns a file
# descriptor to the formatted contents. It is the responsibility of the caller
# to close this descriptor.
def format_file(file, format_command):
    original = open(file, 'r')
    if not format_command:
        return original
    formatted = tempfile.NamedTemporaryFile('w+')
    result = subprocess.run(
        format_command, stdin=original, stdout=formatted)
    original.close()
    if result.returncode != 0:
        print(f"failed to run {format_command}")
        sys.exit(result.returncode)
    formatted.seek(0)
    return formatted


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
    parser.add_argument(
        '--format-command',
        help=
        'A command that reformats goldens (from stdin) before comparison with the candidate',
        nargs='+',
    )
    parser.add_argument(
        '--ignore-space-change',
        help='Whether to ignore changes in the amount of white space',
        action='store_true')
    args = parser.parse_args()

    any_comparison_failed = False
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

        if os.path.exists(golden):
            with format_file(golden, args.format_command) as formatted:
                current_comparison_failed = not compare(
                    candidate, formatted.name, args.ignore_space_change)
        else:
            current_comparison_failed = True

        if current_comparison_failed:
            any_comparison_failed = True
            type = 'Warning' if args.warn or args.bless else 'Error'
            print('%s: Golden file mismatch' % type)

            if args.bless:
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
