#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import sys


def trim_comments_and_redundant_whitespace(source):
    normalized = ''
    for line in source.splitlines():
        # remove comments
        line = re.sub(r'//.*$', '', line)

        # collapse spaces
        line = re.sub(r'\s+', ' ', line.strip())

        if line:
            normalized += line + '\n'

    return normalized


def main():
    parser = argparse.ArgumentParser(
        'Merges FIDL files to a single normalized output.')
    parser.add_argument('--out', help='Path to the output file', required=True)
    parser.add_argument('--files', help='List of library sources', nargs='+')
    args = parser.parse_args()

    with open(args.out, 'w') as out_file:
        for f in args.files:
            with open(f, 'r') as in_file:
                contents = str(in_file.read())
                normalized = trim_comments_and_redundant_whitespace(contents)
                out_file.write(normalized)

    return 0


if __name__ == '__main__':
    sys.exit(main())
