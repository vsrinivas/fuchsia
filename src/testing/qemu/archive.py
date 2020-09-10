#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys
import tarfile
import os


def main():
    parser = argparse.ArgumentParser('Archives a directory')
    parser.add_argument('--src',
                        help='Path to the directory to archive',
                        required=True)
    parser.add_argument('--dst',
                        help='Path to the archive',
                        required=True)
    parser.add_argument('--depfile',
                        help='Path to dependency file',
                        required=True)
    args = parser.parse_args()

    deps = []

    with tarfile.open(args.dst, "w:gz") as tar:
        for (dirpath, dirnames, filenames) in os.walk(args.src):
            for filename in filenames:
                path = os.path.join(dirpath, filename)
                deps.append(os.path.relpath(path))
                tar.add(path, arcname=os.path.relpath(path, args.src))

    with open(args.depfile, 'w') as depfile:
        depfile.write('%s: %s\n' % (args.dst, ' '.join(sorted(deps))))

    return 0


if __name__ == "__main__":
  sys.exit(main())
