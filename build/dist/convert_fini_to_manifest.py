#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Converts a distribution manifest to the Fuchsia INI format.'''

import argparse
import json


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
        dist = [
            {
                'source': src,
                'destination': dst,
                'label': args.label
            } for dst, _, src in (
                line.strip().partition('=') for line in input_file.readlines())
        ]
    with open(args.output, 'w') as output_file:
        json.dump(dist, output_file)


if __name__ == '__main__':
    main()
