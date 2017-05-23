#!/usr/bin/env python

"""
Copyright 2017 The Fuchsia Authors. All rights reserved.
Use of this source code is governed by a BSD-style license that can be
found in the LICENSE file.

This script is a wrappre for the manifest module that allows users
to build minfs images from system.bootfs.manifest files.

"""

import argparse

import manifest

def main():
    parser = argparse.ArgumentParser(description=("Copy build artifacts to minfs "
                                                  "formatted disk images using "
                                                  "system.bootfs.manifest files as "
                                                  "input."))

    parser.add_argument('--disk_path', action='store', required=True,
                        help=("A minfs formatted disk image where the manifest "
                              "should be unpacked. Use `minfs create` to format "
                              "a file/device as minfs before passing it as an "
                              "argument to this script."))

    parser.add_argument('--minfs_path', action='store', required=True,
                        help="The location of the host-compiled minfs binary")

    parser.add_argument('--file_manifest', action='store', required=True,
                        help="Location of the primary file manifest.")

    args = parser.parse_args()

    n_files_copied = manifest.build_minfs_image(args.file_manifest,
                                                args.disk_path,
                                                args.minfs_path)

    print ("Copied %d files" % n_files_copied)

if __name__ == "__main__":
    main()