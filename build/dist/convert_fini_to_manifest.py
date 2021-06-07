#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Converts a distribution manifest to the Fuchsia INI format.'''

import argparse
import json

import distribution_manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', help='Path to FINI manifest', required=True)
    parser.add_argument(
        '--output', help='Path to distribution entries manifest', required=True)
    parser.add_argument(
        '--label',
        help='GN label to set on distribution entries',
        required=True)
    args = parser.parse_args()

    with open(args.input, 'r') as input_file:
        dist_entries = distribution_manifest.convert_fini_manifest_to_distribution_entries(
            input_file.readlines(), args.label)

    with open(args.output, 'w') as output_file:
        output_file.write(
            distribution_manifest.distribution_entries_to_string(dist_entries))


if __name__ == '__main__':
    main()
