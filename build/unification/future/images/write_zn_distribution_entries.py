#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Turns a series of unification manifests into metadata consumable by the
   distribution_manifest template.
   '''

import argparse
import json
import sys


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
            result.append({'destination': destination, 'source': source})
    with open(args.output, 'w') as output_file:
        json.dump(
            sorted(result),
            output_file,
            indent=2,
            sort_keys=True,
            separators=(',', ': '))


if __name__ == '__main__':
    sys.exit(main())
