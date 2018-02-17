#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cmdline', help='Kernel cmdline string')
    parser.add_argument('--block', action='append', help='Block device spec')
    parser.add_argument('filename', help='Path to output filename')
    args = parser.parse_args()

    config = {}
    if args.cmdline:
        config['cmdline'] = args.cmdline
    if args.block:
        config['block'] = args.block

    with open(args.filename, 'w') as f:
        json.dump(config, f, indent=4, separators=(',', ': '))

    return 0


if __name__ == "__main__":
    sys.exit(main())
