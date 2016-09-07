#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import paths
import platform
import re
import subprocess
import sys


def read_build_id(readobj, path):
    buildid_re = re.compile('.*Build ID: ([0-9a-z]+)$')
    if os.path.isfile(path):
        try:
            readobj_cmd = [readobj, '-elf-output-style=GNU', '-notes', path]
            readobj_output = subprocess.check_output(
                readobj_cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError:
            return None
        for readobj_line in readobj_output.split('\n'):
            match = buildid_re.match(readobj_line)
            if match is not None:
                return match.group(1)
    return None


def readobj_path():
    toolchain_name = 'clang+llvm-x86_64-%s' % platform.system().lower()
    return os.path.join(
        paths.TOOLCHAIN_PATH, toolchain_name, 'bin', 'llvm-readobj')


def main():
    parser = argparse.ArgumentParser(
        description='Make a bootfs for loading into Magenta')
    parser.add_argument('--output-file', help='Place to put built userfs')
    parser.add_argument(
        '--build-id-map', help='Place to put mapping from build id to paths')
    parser.add_argument('--manifest', help='Location of manifest')
    args = parser.parse_args()

    readobj = readobj_path()
    buildids = []
    with open(args.manifest) as manifest_contents:
        for line in manifest_contents:
            equal_sign = line.find('=')
            if equal_sign == -1:
                continue
            path = line[equal_sign + 1:].strip()
            buildid = read_build_id(readobj, path)
            if buildid:
                # 'path' will be the path to the stripped binary e.g.:
                #   /foo/out/debug-x86-64/happy_bunny_test
                # We want the path to the unstripped binary which is in:
                #   /foo/out/debug-x86-64/exe.unstripped/happy_bunny_test
                # or
                #   /foo/out/debug-x86-64/lib.unstripped/happy_bunny_test
                # if it exists and matches the buildid.
                path_dir = os.path.dirname(path)
                path_base = os.path.basename(path)
                unstripped_locations = ['exe.unstripped', 'lib.unstripped']
                for location in unstripped_locations:
                    unstripped_path = os.path.join(path_dir,
                                                   location,
                                                   path_base)
                    unstripped_buildid = read_build_id(readobj,
                                                       unstripped_path)
                    if unstripped_buildid == buildid:
                        path = unstripped_path
                        break
                buildids.append('%s %s\n' % (buildid, path))
    with open(args.build_id_map, 'w') as build_id_file:
        build_id_file.writelines(buildids)
    mkbootfs_cmd = [paths.MKBOOTFS_PATH, '-o', args.output_file, args.manifest]
    return subprocess.call(mkbootfs_cmd)

if __name__ == '__main__':
    sys.exit(main())
