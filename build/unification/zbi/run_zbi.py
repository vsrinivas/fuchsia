#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Runs the zbi tool, taking care of unwrapping response files.'''

import argparse
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--zbi', help='Path to the zbi tool', required=True)
    parser.add_argument(
        '--depfile', help='Path to the depfile generated for GN', required=True)
    parser.add_argument(
        '--rspfile',
        help='Path to the response file for the zbi tool',
        required=True)
    args, zbi_args = parser.parse_known_args()

    intermediate_depfile = args.depfile + ".intermediate"

    # Run the zbi tool.
    command = [
        args.zbi,
        '--depfile',
        intermediate_depfile,
    ] + zbi_args
    with open(args.rspfile, 'r') as rspfile:
        command.extend([l.strip() for l in rspfile.readlines()])
    subprocess.check_call(command)

    # Write the final depfile.
    with open(intermediate_depfile, 'r') as depfile:
        contents = depfile.read().strip()
    with open(args.depfile, 'w') as final_depfile:
        final_depfile.write(contents + ' ' + args.rspfile)


if __name__ == '__main__':
    main()
