#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import paths
import platform
import re
import subprocess
import sys


BUILDID_RE = re.compile('.*Build ID: ([0-9a-z]+)$')

def read_build_id(path):
    if os.path.isfile(path):
        try:
            readelf_output = subprocess.check_output(
                [paths.build_tool('clang', 'llvm-readelf'), '-n', path],
                stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError:
            return None
        for readelf_line in readelf_output.split('\n'):
            match = BUILDID_RE.match(readelf_line)
            if match is not None:
                return match.group(1)
    return None


def main():
    buildids = []
    build_id_file = sys.argv[1]
    for path in sys.argv[2:]:
        buildid = read_build_id(path)
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
                unstripped_buildid = read_build_id(unstripped_path)
                if unstripped_buildid == buildid:
                    path = unstripped_path
                    break
            buildids.append('%s %s\n' % (buildid, path))
    with open(build_id_file, 'w') as build_id_file:
        build_id_file.writelines(buildids)

if __name__ == '__main__':
    sys.exit(main())
