#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Verify that two input files are identical."""

import argparse
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, help='Output stamp file.')
    parser.add_argument('file_a')
    parser.add_argument('file_b')

    args = parser.parse_args()

    with open(args.file_a) as f:
        a = f.read()

    with open(args.file_b) as f:
        b = f.read()

    if a != b:
        print('Files are different!', file=sys.stderr)
        return 1

    with open(args.output, 'w') as f:
        f.write('')

    return 0


if __name__ == "__main__":
    sys.exit(main())
