#!/usr/bin/env python2.7
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys


def main():
    parser = argparse.ArgumentParser('Write a package file')
    parser.add_argument('--name', help='Package name', required=True)
    parser.add_argument('--version', help='Package version', required=True)
    parser.add_argument('path', help='Path to the package file')
    args = parser.parse_args()

    with open(args.path, 'w') as f:
        json.dump(
            {
                'name': args.name,
                'version': args.version
            },
            f,
            separators=(',', ':'),
            sort_keys=True)

    return 0


if __name__ == '__main__':
    sys.exit(main())
