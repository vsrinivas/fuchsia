#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import shutil
import subprocess

# Runs a specified formatter to reformat a golden file.


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', help='Source file to format', required=True)
    parser.add_argument(
        '--output', help='Path to emit the formatted output to', required=True)
    parser.add_argument(
        '--formatter', help='Executable to reformat golden files')
    parser.add_argument(
        'formatter_args',
        nargs='*',
        help='Args to pass to the formatter, passed as positional arguments')
    args = parser.parse_args()

    if not args.formatter:
        shutil.copyfile(args.source, args.output)
        return

    cmd = [args.formatter] + args.formatter_args
    with open(args.source) as infile, open(args.output, 'w') as outfile:
        subprocess.run(cmd, stdin=infile, stdout=outfile)


if __name__ == '__main__':
    main()
