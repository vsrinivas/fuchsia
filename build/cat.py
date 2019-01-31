#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

def parse_args():
    parser = argparse.ArgumentParser(description='Concat files.')
    parser.add_argument('-i', action='append', dest='inputs', default=[],
                          help='Input files', required=True)
    parser.add_argument('-o', dest='output', help='Output file', required=True)
    args = parser.parse_args()
    return args

def main():
    args = parse_args()
    with open(args.output, 'w') as outfile:
      for fname in args.inputs:
        with open(fname) as infile:
            outfile.write(infile.read())


if __name__ == '__main__':
    sys.exit(main())
