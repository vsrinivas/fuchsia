#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Turns a series of unification manifests into metadata consumable by the
   distribution_manifest template.
   '''

import argparse
import collections
import json
import sys


Entry = collections.namedtuple('Entry', ['destination', 'source'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--file', help='Path to a unification manifest', action='append')
    parser.add_argument(
        '--output', help='Path to the result file', required=True)
    args = parser.parse_args()

    result = []
    for manifest in args.file:
        with open(manifest, 'r') as manifest_file:
            lines = manifest_file.readlines()
        for line in lines:
            destination, source = line.strip().split('=', 1)
            result.append(Entry(destination, source))
    with open(args.output, 'w') as output_file:
        json.dump(
            [dict(destination=r.destination, source=r.source) for r in sorted(result)],
            output_file,
            indent=2,
            sort_keys=True,
            separators=(',', ': '))


if __name__ == '__main__':
    sys.exit(main())
