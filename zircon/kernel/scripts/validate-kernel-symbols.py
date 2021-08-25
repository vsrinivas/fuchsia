#!/usr/bin/env python3.8
#
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
validate that zircon.elf does not contain banned symbols

If zircon.elf contains no guard variables for function scoped statics,
generate a depfile and exit with 0.  Otherwise, print the mangled
symbol names for function scoped static guard variables and exit with
a non-zero result.

"""

import argparse
import io
import os
import subprocess
import sys

# Guard variables for function scoped statics start with _ZGVZ.
BANNED_PREFIX = b'_ZGVZ'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('nm_bin', help='path to nm binary')
    parser.add_argument(
        'zircon_elf_rsp',
        help='path to a file containing the path to zircon.elf')
    parser.add_argument('output', help='path to the output file to create')
    parser.add_argument('depfile', help='path to the depfile to generate')
    args = parser.parse_args()

    # Write the depfile.
    with open(args.depfile, 'w') as depfile:
        print(
            '{:s}: {:s}'.format(args.output, args.zircon_elf_rsp), file=depfile)

    # Read the path to zircon.elf.
    with open(args.zircon_elf_rsp, 'r') as zircon_rsp_elf:
        zircon_elf = zircon_rsp_elf.read().rstrip('\n')

    # Create a list of guard variables for function scoped statics.
    nm = subprocess.Popen(
        [args.nm_bin, '-j', zircon_elf], stdout=subprocess.PIPE)
    banned_guard_variables = list(
        map(
            lambda x: x.decode('UTF-8').rstrip('\n'),
            filter(lambda x: x.startswith(BANNED_PREFIX), nm.stdout)))

    if len(banned_guard_variables) > 0:
        print(
            '{:s}: ERROR: {:s} contains non-trivial function scoped statics. Mangled guard variable symbol names follow:'
            .format(parser.prog, args.zircon_elf))
        print(*banned_guard_variables, sep='\n')
        sys.exit(1)

    # None found.  Write an empty output file.
    with open(args.output, 'w') as file:
        os.utime(file.name, None)


if __name__ == '__main__':
    main()
