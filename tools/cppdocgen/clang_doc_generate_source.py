#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

# Used as part of the clang_doc template to generate a .cc file that includes a given list of
# headers.
#
# Pass the .cc file to write as --out-cc=<file> and the headers to include as the remaining
# arguments.
#
# Example:
#
#    clang_doc_generate_source.py --out-cc=foo.cc a.h b.h


def main():
    parser = argparse.ArgumentParser(
        'Generates a .cc file that #includes a given set of headers.')
    parser.add_argument(
        '--out-cc', help='The name of the .cc file to generate.', required=True)
    parser.add_argument(
        'files', help='Header file names to output', nargs='+', metavar='file')
    args = parser.parse_args()

    with open(args.out_cc, 'w') as output:
        for f in args.files:
            output.write('#include "%s"\n' % f)


if __name__ == '__main__':
    sys.exit(main())
