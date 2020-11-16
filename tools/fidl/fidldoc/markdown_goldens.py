#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
check golden files for end-to-end fidldoc tests

This script reads the jsonir_goldens.txt file whcih contains the list of JSON
IR golden files and makes sure there's a corresponding Markdown golden for each
of them. If a Markdown golden is missing, the script returns an error,
with instructions to move forward and a link to the documentation to generate
the missing goldens.
"""
import argparse
import os
from os import path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--input', metavar='FILE', help='path to jsonir_goldens.txt')
    parser.add_argument(
        '--goldens', metavar='FOLDER', help='path to markdown goldens')
    parser.add_argument(
        '--stamp', help="Path to stamp file to write after completion")
    parser.add_argument(
        '--depfile', help="Path to depfile to write if also stamping")
    args = parser.parse_args()

    deps = []

    with open(args.input, 'r') as f:
        # Each line of this file points to a JSON IR golden
        lines = f.readlines()
        for line in lines:
            line = line.rstrip()  # strip the trailing \n

            # Markdown goldens have the same filename as JSON IR goldens
            # with a ".md" extension
            golden_name = line + '.md'
            golden_file = path.join(args.goldens, golden_name)
            deps.append(golden_file)

            # No corresponding Markdown golden, let's stop the build and
            # provide instructions to fix the issue
            if not path.exists(golden_file):
                print('No Markdown golden file for {}'.format(line))
                print('To continue building, create the following file:')
                print(golden_file)
                print(
                    'Once the build is complete, follow instructions to regenerate goldens:'
                )
                print(
                    'https://fuchsia.googlesource.com/fuchsia/+/HEAD/tools/fidl/fidldoc/README.md'
                )
                return 1

    if args.stamp:
        with open(args.stamp, 'w') as stamp:
            stamp.truncate()
        if args.depfile:
            with open(args.depfile, 'wt') as depfile:
                depfile.write(args.stamp + ": " + " ".join(deps) + "\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
