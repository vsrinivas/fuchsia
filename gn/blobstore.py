#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys
import subprocess

# TODO(ZX-1392): this entire script can be replaced by a direct invocation of
# blobstore once it supports auto-sizing and repeated commands.

def compute_size(archive, manifest):
    """
    Compute the size of the blobstore image, given a package archive and the
    manifest of files it contains
    """
    size = os.path.getsize(archive)
    with open(manifest) as manifest:
        for line in manifest:
            line = line.strip()
            if line.startswith("#"):
                continue
            source = line.split("=")[1]
            size += os.path.getsize(source)
    # 1.4 should provide sufficient space for blobstore metadata
    return size * 14 / 10

def main():
    """
    Construct blobstore images at --image based on the input --manifest and --add.

    Wrap zircons blobstore host tool in order to precompute the target file size.
    This script is temporary, after ZX-1392 it can be replaced with a direct
    call to blobstore(1).
    """
    parser = argparse.ArgumentParser("Generates a blobstore partition image")
    parser.add_argument("--blobstore",
                        help="Path to the blobstore binary",
                        required=True)
    parser.add_argument("--image",
                        help="Path to output image",
                        required=True)
    parser.add_argument("--manifest",
                        help="Path to manifest to add",
                        required=True)
    parser.add_argument("--add",
                        help="Path to file to add",
                        required=True)
    args = parser.parse_args()

    if os.path.exists(args.image):
        os.remove(args.image)

    size = compute_size(args.add, args.manifest)

    # Note: These calls could be aggregated into one if zircon/90343 is
    # submitted, but will likely instead be superseded by ZX-1392.
    subprocess.check_call([args.blobstore,
                           "{}@{}".format(args.image, size),
                           "create"])
    subprocess.check_call([args.blobstore,
                           args.image,
                           "manifest", args.manifest])
    subprocess.check_call([args.blobstore,
                           args.image,
                           "add", args.add])

    return 0

if __name__ == '__main__':
    sys.exit(main())
