#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys
import shlex

def main():
    parser = argparse.ArgumentParser(
        description='Product a blobfs manifest from a set of blobs.json',
        fromfile_prefix_chars='@')
    parser.convert_arg_line_to_args = shlex.split
    parser.add_argument('--output', required=True,
                        help='Output manifest path')
    parser.add_argument('--depfile', required=True,
                        help='Dependency file output path')
    parser.add_argument('--input', action='append', default=[],
                        help='Input blobs.json, repeated')

    args = parser.parse_args()

    all_blobs = dict()

    with open(args.depfile, 'w') as depfile:
        depfile.write(args.output)
        depfile.write(':')
        for path in args.input:
            depfile.write(' ' + path)
            with open(path) as input:
                blobs = json.load(input)
                for blob in blobs:
                    src = blob['source_path']
                    all_blobs[blob['merkle']] = src
                    depfile.write(' ' + src)

    with open(args.output, 'w') as output:
        for merkle, src in all_blobs.items():
            output.write('%s=%s\n' % (merkle, src))


if __name__ == '__main__':
    sys.exit(main())
