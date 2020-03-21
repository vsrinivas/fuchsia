#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''Compares the contents of two zbi files, erroring if they differ.'''

from __future__ import print_function

import argparse
import difflib
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--tool',
                        help='Path to the zbi tool',
                        required=True)
    parser.add_argument('--reference-zbi',
                        help='Path to the original zbi',
                        required=True)
    parser.add_argument('--generated-zbi',
                        help='Path to the generated zbi',
                        required=True)
    parser.add_argument('--stamp',
                        help='Path to the stamp file signalling success',
                        required=True)
    args = parser.parse_args()

    def get_contents(path):
        command = [args.tool, '-tv', path]
        return subprocess.check_output(command).splitlines()

    reference = get_contents(args.reference_zbi)
    generated = get_contents(args.generated_zbi)
    if reference != generated:
        print('Error: zbi\'s don\'t match')
        diff = difflib.Differ().compare(reference, generated)
        print('\n'.join(l.strip() for l in diff))
        return 1

    with open(args.stamp, 'w') as stamp_file:
        stamp_file.write('Success!')
    return 0


if __name__ == '__main__':
    sys.exit(main())
