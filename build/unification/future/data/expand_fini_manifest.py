#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''Reads the contents of a manifest file and expands file references within.'''

import argparse


def expand(manifest):
    with open(manifest, 'r') as manifest_file:
        lines = manifest_file.readlines()
    result = []
    for line in lines:
        # Format: foo/bar=path/on/disk/for/bar
        if '=' in line:
            result.append(line)
            continue
        # Format: path/on/disk/for/manifest
        # That manifest contains lines of the above format.
        result.extend(expand(line.strip()))
    return list(set(result))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--input',
                        help='Path to original manifest',
                        required=True)
    parser.add_argument('--output',
                        help='Path to the unrolled manifest',
                        required=True)
    args = parser.parse_args()

    all_lines = expand(args.input)
    with open(args.output, 'w') as output_file:
        output_file.writelines(all_lines)


if __name__ == '__main__':
    main()
