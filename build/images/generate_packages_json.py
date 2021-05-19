#!/usr/bin/env python3.8
#
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Generates the packages.json file in the update package.

import argparse
import json
import sys


def main():
    parser = argparse.ArgumentParser(
        description='Generates the packages.json file in the update package',
    )
    parser.add_argument(
        '--input_file',
        type=argparse.FileType('r'),
        required=True,
    )
    parser.add_argument(
        '--output_file',
        type=argparse.FileType('w'),
        required=True,
    )
    parser.add_argument(
        '--depfile',
        type=argparse.FileType('w'),
        required=True,
    )
    args = parser.parse_args()

    pkgurls = []
    deps = []
    lines = args.input_file.readlines()
    lines.sort()
    for line in lines:
        package, merkle_path = line.strip().split('=', 1)
        deps.append(merkle_path)
        with open(merkle_path, 'r') as merkle_file:
            merkle = merkle_file.read().strip()
            pkgurls.append(
                'fuchsia-pkg://fuchsia.com/{}?hash={}'.format(package, merkle),
            )

    args.depfile.write('{}: {}\n'.format(args.output_file.name, ' '.join(deps)))

    args.output_file.write(
        json.dumps(
            {
                'version': '1',
                'content': pkgurls,
            }, separators=(',', ':')))


if __name__ == '__main__':
    sys.exit(main())
