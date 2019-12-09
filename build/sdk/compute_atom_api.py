#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import hashlib
import json
import sys

# Builds a representation of the atom's "API" by computing hashes for the files
# that contribute to the atom's API.
#
# This is only meant to be provide a (very) rough signal of changes in an atom.
# Templates that generate sdk_atom instances should generate that file
# themselves instead.


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--file',
        help='A (destination <-- source) mapping',
        action='append',
        nargs=2)
    parser.add_argument(
        '--output', help='The API file to generate', required=True)
    args = parser.parse_args()

    api = {}
    for destination, source in args.file:
        with open(source, 'r') as source_file:
            hash = hashlib.md5(source_file.read()).hexdigest()
        api[destination] = hash

    with open(args.output, 'w') as output_file:
        # Specify `separators` to prevent whitespaces at the end of lines.
        json.dump(
            api, output_file, indent=2, sort_keys=True, separators=(',', ': '))

    return 0


if __name__ == '__main__':
    sys.exit(main())
