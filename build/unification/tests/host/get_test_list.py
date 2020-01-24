#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source',
                        help='Path to the file containing test specs',
                        required=True)
    parser.add_argument('--output',
                        help='Path to the generated file with test names',
                        required=True)
    args = parser.parse_args()

    with open(args.source, 'r') as source_file:
        specs = json.load(source_file)
    names = sorted(map(lambda s: s['test']['name'], specs))
    with open(args.output, 'w') as output_file:
        json.dump(names, output_file)

    return 0


if __name__ == '__main__':
    sys.exit(main())
