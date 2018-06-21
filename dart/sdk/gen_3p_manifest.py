#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys

FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # build
    os.path.dirname(             # dart
    os.path.dirname(             # sdk
    os.path.abspath(__file__)))))

sys.path += [os.path.join(FUCHSIA_ROOT, 'third_party', 'pyyaml', 'lib')]
import yaml


# The list of packages that should be pulled from a Flutter SDK instead of pub.
FLUTTER_PACKAGES = [
    'flutter',
    'flutter_driver',
    'flutter_test',
    'flutter_tools',
]


def main():
    parser = argparse.ArgumentParser('Builds a manifest file with 3p deps')
    parser.add_argument('--out',
                        help='Path to the output file',
                        required=True)
    parser.add_argument('--name',
                        help='Name of the original package',
                        required=True)
    parser.add_argument('--specs',
                        help='Path to spec files of 3p dependencies',
                        nargs='*')
    args = parser.parse_args()

    deps = {}
    for spec in args.specs:
        with open(spec, 'r') as spec_file:
            manifest = yaml.safe_load(spec_file)
            name = manifest['name']
            tag = '3p:%s' % name
            if name in FLUTTER_PACKAGES:
                deps[tag] = 'flutter_sdk'
            else:
                if 'version' not in manifest:
                    raise Exception('%s does not specify a version.' % spec)
                deps[tag] = manifest['version']

    with open(args.out, 'w') as out_file:
        json.dump(deps, out_file)

    return 0


if __name__ == '__main__':
    sys.exit(main())
