#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import itertools
import shlex
import sys

# Takes a response file meant for fidlc, extracts the file references it
# contains, and generates a depfile with these file references.

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--rsp-file',
                        help='Path to the rsp file with file references',
                        required=True)
    parser.add_argument('--dep-target',
                        help='Path to the file at the root of the depfile',
                        required=True)
    parser.add_argument('--dep-file',
                        help='Path to the depfile to generate',
                        required=True)
    args = parser.parse_args()

    with open(args.rsp_file, 'r') as rsp_file:
        lines = rsp_file.readlines()
        # Generate something looking like a proper command line.
        command = shlex.split(' '.join(l.strip() for l in lines))
        # Use argparse to parse that command line!
        files_parser = argparse.ArgumentParser()
        files_parser.add_argument('--files',
                                  nargs='*',
                                  action='append')
        # Flatten the resulting list of lists.
        files = list(itertools.chain(*files_parser.parse_args(command).files))

    with open(args.dep_file, 'w') as dep_file:
        dep_file.write('%s: %s' % (args.dep_target, ' '.join(files)))

    return 0


if __name__ == '__main__':
    sys.exit(main())
