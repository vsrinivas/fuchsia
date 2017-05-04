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
# Returns whether it copied any files.
def copy_tree(source, dest):
    copied_anything = False
    for entry in os.walk(source):
        dirpath, dirnames, filenames = entry
        # Skip the 'debug-info' directory as that contains files that change even when there's no
        # need to recompile or relink.
        if 'debug-info' in dirnames:
            dirnames.remove('debug-info')
        reldir = os.path.relpath(dirpath, source)
        destdir = os.path.join(dest, reldir)
        if not os.path.exists(destdir):
            os.makedirs(destdir)
        for filename in filenames:
            source_file = os.path.join(dirpath, filename)
            dest_file = os.path.join(destdir, filename)
            if os.path.exists(source_file) and os.path.exists(dest_file) and filecmp.cmp(source_file, dest_file):
                continue
            shutil.copy(source_file, dest_file)
            sys.stderr.write('Copied %s\n' % source_file)
            copied_anything = True
    return copied_anything


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--lib_dir')
    parser.add_argument('--toolchain_sysroot')
    parser.add_argument('--magenta_sysroot')
    parser.add_argument('--sysroot')
    parser.add_argument('--sysroot_stamp')
    args = parser.parse_args()

    # Copy everything from the magenta sysroot.
    copied_anything = copy_tree(args.magenta_sysroot, args.sysroot)
    copied_anything = copy_tree(args.toolchain_sysroot, args.sysroot) or copied_anything

    if copied_anything:
        stamp_path = os.path.relpath(args.sysroot_stamp)
        with open(stamp_path, 'w') as stamp_file:
            stamp_file.truncate()

    return 0


if __name__ == '__main__':
    sys.exit(main())
