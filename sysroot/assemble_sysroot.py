#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import filecmp
import os
import shutil
import stat
import subprocess
import sys

# Copies all files in source to dest excluding any files at the top level whose
# names are in ignore_list. Does not copy files that appear to be identical.
# This is very similar to shutil.copytree except that it compares files before
# copying to avoid touching files whose contents match.
def copy_tree(source, dest, ignore_list):
    for entry in os.walk(source):
        dirpath, dirnames, filenames = entry
        reldir = os.path.relpath(dirpath, source)
        destdir = os.path.join(dest, reldir)
        if not os.path.exists(destdir):
            os.makedirs(destdir)
        for filename in filenames:
            if dirpath == source and filename in ignore_list:
                continue
            source_file = os.path.join(dirpath, filename)
            dest_file = os.path.join(destdir, filename)
            if os.path.exists(source_file) and os.path.exists(dest_file) and filecmp.cmp(source_file, dest_file):
                continue
            shutil.copy(source_file, dest_file)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--lib_dir')
    parser.add_argument('--toolchain_sysroot')
    parser.add_argument('--magenta_sysroot')
    parser.add_argument('--sysroot')
    parser.add_argument('--sysroot_stamp')
    args = parser.parse_args()

    # Copy everything from the magenta sysroot.
    copy_tree(args.magenta_sysroot, args.sysroot, [])
    copy_tree(args.toolchain_sysroot, args.sysroot, [])

    stamp_path = os.path.relpath(args.sysroot_stamp)
    with open(stamp_path, 'w') as stamp_file:
        stamp_file.truncate()

    return 0


if __name__ == '__main__':
    sys.exit(main())
