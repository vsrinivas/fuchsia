#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out',
                        help='Path to the output file',
                        required=True)
    parser.add_argument('--boards',
                        help='Name of the supported boards',
                        nargs='*')
    args = parser.parse_args()

    with open(args.out, 'w') as out_file:
        out_file.write('\n'.join(args.boards))


if __name__ == '__main__':
    sys.exit(main())
