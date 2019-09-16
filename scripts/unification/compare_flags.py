#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script prints out a summary of the differences between the GN and ZN builds on the topic of
# C/C++ compilation. The data is extracted by identifying in each build a target considered
# representative of the compilation environment in that build.
# This is meant to be a temporary tool as the build unification work proceeds. Once fuchsia.git
# builds with a single GN/ninja pass, the script can be deleted.

import argparse
import json
import os
import subprocess
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FUCHSIA_ROOT = os.path.dirname(  # $root
    os.path.dirname(             # scripts
    SCRIPT_DIR))                 # unification

GN_TARGET = '//src/lib/fxl:fxl_logging(//build/toolchain/fuchsia:arm64-shared)'
ZN_TARGET = '//system/ulib/fdio:fdio.shared(//public/gn/toolchain:user-arm64-clang.shlib)'


def diff_lists(gn_object, zn_object, dimension):
    print('--------------------------')
    print('Parameter [' + dimension + ']')
    gn_set = set(gn_object[dimension])
    zn_set = set(zn_object[dimension])
    gn_only = gn_set - zn_set
    zn_only = zn_set - gn_set
    if not gn_only and not zn_only:
        print('Identical!')
        return
    if gn_only:
        print('GN only:')
        for item in sorted(gn_only):
            print('  ' + item)
    if zn_only:
        print('ZN only:')
        for item in sorted(zn_only):
            print('  ' + item)


def main():
    parser = argparse.ArgumentParser(
            description='Compares C/C++ compilation flags between the GN and ZN builds')
    parser.add_argument('--build-dir',
                        help='Path to the GN build dir',
                        required=True)
    args = parser.parse_args()

    source_dir = FUCHSIA_ROOT
    gn_build_dir = os.path.abspath(args.build_dir)
    zn_build_dir = gn_build_dir + '.zircon'

    if sys.platform.startswith('linux'):
        platform = 'linux-x64'
    elif sys.platform.startswith('darwin'):
        platform = 'mac-x64'
    else:
        print('Unsupported platform: %s' % sys.platform)
        return 1
    gn_binary = os.path.join(source_dir, 'prebuilt', 'third_party', 'gn',
                             platform, 'gn')

    base_command = [gn_binary, 'desc', '--format=json']

    print('Getting GN data... [' + GN_TARGET + ']')
    gn_command = base_command + [gn_build_dir, GN_TARGET]
    result = subprocess.check_output(gn_command, cwd=source_dir)
    gn_data = json.loads(result)

    print('Getting ZN data... [' + ZN_TARGET + ']')
    zircon_dir = os.path.join(source_dir, 'zircon')
    zn_command = base_command + ['--root=' + zircon_dir, zn_build_dir, ZN_TARGET]
    result = subprocess.check_output(zn_command, cwd=source_dir)
    zn_data = json.loads(result)

    gn_object = gn_data.items()[0][1];
    zn_object = zn_data.items()[0][1];

    diff_lists(gn_object, zn_object, 'cflags');
    diff_lists(gn_object, zn_object, 'cflags_c');
    diff_lists(gn_object, zn_object, 'cflags_cc');

    return 0


if __name__ == '__main__':
    exit(main())
