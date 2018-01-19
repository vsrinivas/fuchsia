#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Prints the Clang version obtained either from CIPD version file or from the
update script stamp file, whichever exists on the local filesystem.

TODO(TO-723): read the version_file directly when GN supports JSON input file
conversion.

Usage:
  python clang_version.py path/to/version_file path/to/stamp_file
"""

import argparse
import json
import os
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("version_file", help="Path to version file")
    parser.add_argument("stamp_file", help="Path to stamp file")
    args = parser.parse_args()

    if os.path.exists(args.version_file):
        with open(args.version_file) as f:
            version = json.load(f)
            print version['instance_id']
    elif os.path.exists(args.stamp_file):
        with open(args.stamp_file) as f:
            print f.read().strip()
    else:
        raise Exception('Either version or stamp file must exist')

    return 0


if __name__ == '__main__':
    sys.exit(main())
