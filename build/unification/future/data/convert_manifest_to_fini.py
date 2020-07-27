#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Converts a distribution manifest to the Fuchsia INI format.'''

import argparse
import json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--input', help='Path to original manifest', required=True)
    parser.add_argument(
        '--output', help='Path to the formatted manifest', required=True)
    args = parser.parse_args()

    with open(args.input, 'r') as input_file:
        objects = json.load(input_file)
    lines = sorted(o['destination'] + '=' + o['source'] + '\n' for o in objects)
    with open(args.output, 'w') as output_file:
        output_file.writelines(lines)


if __name__ == '__main__':
    main()
