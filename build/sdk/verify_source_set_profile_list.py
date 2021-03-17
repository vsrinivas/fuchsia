#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Verify that the in-tree and generated SDK source set profile lists are identical.
And print a useful humand-friendly error message otherwise."""

import argparse
import filecmp
import sys
import os


def main():
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument('--output', required=True, help='Output stamp file.')
    parser.add_argument(
        '--generated',
        required=True,
        help='Path to generated profile list file.')
    parser.add_argument(
        '--source', required=True, help='Path to in-tree profile list file.')

    args = parser.parse_args()

    if not filecmp.cmp(args.generated, args.source):
        print(
            '''ERROR: The list of SDK sources in [1] is not up-to-date. This will happen when
you modify the BUILD.gn rules for sdk_source_set() instances. To update the file and fix this
error, simply copy the content of [2] into it, then rebuild.

[1] %s
[2] %s

Example shell command:

  cp %s %s

For more information why this is needed, see //build/config/profile/sdk.gni.
''' % (
                args.source, args.generated, os.path.abspath(
                    args.generated), os.path.abspath(args.source)),
            file=sys.stderr)
        return 1

    # Write empty stamp file on success.
    with open(args.output, 'w') as f:
        f.write('')

    return 0


if __name__ == '__main__':
    sys.exit(main())
