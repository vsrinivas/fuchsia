#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Transforms a manifest produced by the ZN build into a JSON structure to be
   turned into a scope by GN.'''

import argparse
import json
import os
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--zircon-build-dir',
        help='Path to the root of the ZN build',
        required=True)
    parser.add_argument(
        '--build-dir', help='Path to the root of the GN build', required=True)
    parser.add_argument(
        '--label',
        help='GN label of the target hosting the metadata',
        required=True)
    parser.add_argument(
        '--entry', help='A manifest line to format', action='append')
    args = parser.parse_args()

    result = []
    for entry in args.entry:
        destination, source = entry.split('=', 1)
        source = os.path.relpath(
            os.path.join(args.zircon_build_dir, source), args.build_dir)
        result.append(
            {
                'source': source,
                'destination': destination,
                'label': args.label,
            })

    print(json.dumps(result))

    return 0


if __name__ == '__main__':
    sys.exit(main())
