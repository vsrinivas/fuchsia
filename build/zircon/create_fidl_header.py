#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Run the 'fidlc' host tool to generate a C header from a list of libraries given in a response file.
This also takes care of generating a dependency file."""

import argparse
import itertools
import shlex
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--name', required=True, help='Fidl library name')
    parser.add_argument(
        '--c-header', required=True, help='Path to output header file')
    parser.add_argument(
        '--fidlc', required=True, help='Path to fidlc tool binary')
    parser.add_argument(
        '--rsp-file',
        help='Path to the rsp file with file references',
        required=True)
    parser.add_argument(
        '--dep-file', help='Path to the depfile to generate', required=True)
    args = parser.parse_args()

    # Run 'fidlc' to generate the header.
    subprocess.check_call(
        [
            args.fidlc, '--c-header', args.c_header, '--name', args.name,
            '@' + args.rsp_file
        ])

    # Generate the depfile from the content of the response file.
    with open(args.rsp_file, 'r') as rsp_file:
        lines = rsp_file.readlines()
        # Generate something looking like a proper command line.
        command = shlex.split(' '.join(l.strip() for l in lines))
        # Use argparse to parse that command line!
        files_parser = argparse.ArgumentParser()
        files_parser.add_argument('--files', nargs='*', action='append')
        # Flatten the resulting list of lists.
        files = list(itertools.chain(*files_parser.parse_args(command).files))

    with open(args.dep_file, 'w') as dep_file:
        dep_file.write('%s: %s' % (args.c_header, ' '.join(files)))

    return 0


if __name__ == '__main__':
    sys.exit(main())
