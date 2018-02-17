#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys


def main():
    parser = argparse.ArgumentParser('Combine manifests into a single one')
    parser.add_argument('filename', help='Combined manifest name')
    parser.add_argument('manifest', nargs='+',
                        help='Manifest to include in the combined manifest')
    args = parser.parse_args()

    with open(args.filename, 'w') as fp:
        for manifest in args.manifest:
            with open(manifest, 'r') as f:
                fp.write(f.read())

    return 0


if __name__ == '__main__':
    sys.exit(main())
