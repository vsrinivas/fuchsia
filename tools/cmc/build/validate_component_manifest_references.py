#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# Lint as: python3

import argparse
import re
import sys


# TODO(shayba): Make this more robust.
# * Actually parse cmx JSON / cml JSON5,
#   rather than match against formatted files with regular expressions.
# * Introduce more analysis that's aware of runners,
#   rather than bail out when any runners are involved.
#   Or maybe specialize the analysis,
#   for instance for Dart look for *.dlib files under program.data.
# * Validate more paths (data files/directories?)
#   rather than just binary paths.
def main():
    parser = argparse.ArgumentParser(
        'Validate component manifests against package manifests')
    parser.add_argument(
        '--component_manifest',
        required=True,
        type=argparse.FileType('r'),
        help='Path to a component manifest to validate (cmx/cml file)')
    parser.add_argument(
        '--package_manifest',
        required=True,
        type=argparse.FileType('r'),
        help='Path to a package manifest to validate against')
    parser.add_argument(
        '--gn-label',
        required=True,
        help='GN label to include in error messages')
    parser.add_argument(
        '--stamp',
        required=True,
        type=argparse.FileType('w'),
        help='Stamp file')
    args = parser.parse_args()

    dsts = [
        dst for dst, _, src in (
            line.partition('=') for line in args.package_manifest.readlines())
    ]

    # Matches program.binary in any formatted cmx or cml file
    binary_re = re.compile('        "?binary"?: "([^"]*)",?')
    binary_value = None
    for line in args.component_manifest.readlines():
        if "runner" in line:
            # Runners can modify the component's incoming namespace,
            # so don't bother trying to validate.
            return stamp(args)
        m = binary_re.match(line)
        if m:
            binary_value = m.group(1)

    if not binary_value:
        # Nothing to validate
        return stamp(args)

    if binary_value in dsts:
        return stamp(args)

    # Legacy package.gni supports the "disabled test" feature that intentionally
    # breaks component manifests. /shrug
    if binary_value.startswith(
            "test/") and "test/disabled/" + binary_value[5:] in dsts:
        return stamp(args)

    print(f'Error found in: {args.gn_label}')
    print(f'Failed to validate manifest: {args.component_manifest.name}')
    print(f'program.binary="{binary_value}" but {binary_value} is not in deps!')
    print()
    nearest = nearest_match(binary_value, dsts)
    if nearest:
        print(f'Did you mean "{nearest}"?')
        print()
    print('Try any of the following:')
    print('\n'.join(sorted(dsts)))
    return 1


def stamp(args):
    args.stamp.write('Success!')
    return 0


def nearest_match(start, candidates):
    """Finds the nearest match to `start` out of `candidates`."""
    nearest = None
    min_distance = sys.maxsize
    for candidate in candidates:
        distance = minimum_edit_distance(start, candidate)
        if distance < min_distance:
            min_distance = distance
            nearest = candidate
    return nearest


def minimum_edit_distance(s, t):
    """Finds the Levenshtein distance between `s` and `t`."""
    # Dynamic programming table
    rows = len(s) + 1
    cols = len(t) + 1
    dist = [[0 for x in range(cols)] for x in range(rows)]

    # Fastest way to transform to empty string is N deletions
    for i in range(1, rows):
        dist[i][0] = i
    # Fastest way to transform from empty string is N insertions
    for i in range(1, cols):
        dist[0][i] = i

    for col in range(1, cols):
        for row in range(1, rows):
            if s[row - 1] == t[col - 1]:
                cost = 0
            else:
                cost = 1
            dist[row][col] = min(
                dist[row - 1][col] + 1,  # Deletion
                dist[row][col - 1] + 1,  # Insertion
                dist[row - 1][col - 1] + cost)  # Substitution

    return dist[row][col]


if __name__ == '__main__':
    sys.exit(main())
