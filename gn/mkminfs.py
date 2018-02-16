#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
mkminfs.py wraps the minfs host tool to produce minfs images of a size just
large enough to contain the manifest of inputs.
"""

import argparse
import os
import subprocess
import sys

def main():
    parser = argparse.ArgumentParser("Generate a minfs image of a size sufficient to fit the given manifest")
    parser.add_argument("--minfs",
                        help="Path to the minfs tool",
                        required=True)
    parser.add_argument("--manifest",
                        help="The manifest of files to be added to the image",
                        required=True)
    parser.add_argument("--output",
                        help="Path to the image file to be generated",
                        required=True)
    args = parser.parse_args()


    if os.path.exists(args.output):
        os.remove(args.output)

    manifest_dir = os.path.dirname(args.manifest)
    size = 0
    with open(args.manifest) as manifest:
        for line in manifest:
            src = line.strip().split("=")[1]
            if src:
                size += os.path.getsize(os.path.join(manifest_dir, src))

    # Minfs requires some space for metadata, but it's not known exactly how
    # much, this provides sufficient space for most use cases.
    size *= 1.4

    sized_path = "%s@%d" % (args.output, size)

    subprocess.check_call([args.minfs, sized_path, "create"])
    subprocess.check_call([args.minfs, args.output, "manifest", args.manifest])

    return 0

if __name__ == '__main__':
    sys.exit(main())
