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


class Type(object):
    LIB = 'lib'
    DRIVER = 'driver'
    @classmethod
    def all(cls): return [cls.LIB, cls.DRIVER]


class Build(object):
    ZN = 'zn'
    GN = 'gn'


TARGETS = {
    Type.LIB: {
        Build.ZN: '//system/ulib/fdio:fdio.shared(//public/gn/toolchain:user-arm64-clang.shlib)',
        Build.GN: '//src/lib/fxl:fxl_logging(//build/toolchain/fuchsia:arm64-shared)',
    },
    Type.DRIVER: {
        Build.ZN: '//system/ulib/ddktl:ddktl-test.binary._build(//public/gn/toolchain:user-x64-clang.shlib)',
        Build.GN: '//src/media/audio/drivers/virtual_audio:virtual_audio_driver(//build/toolchain/fuchsia:x64-shared)'
    }
}


DIMENSIONS = [
    'cflags',
    'cflags_c',
    'cflags_cc',
]


def diff_lists(gn_object, zn_object, dimension):
    print('--------------------------')
    print('Parameter [' + dimension + ']')
    gn_set = set(gn_object[dimension]) if dimension in gn_object else set()
    zn_set = set(zn_object[dimension]) if dimension in zn_object else set()
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
    parser.add_argument('--type',
                        help='Type of target',
                        choices=Type.all(),
                        default=Type.LIB)
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

    gn_target = TARGETS[args.type][Build.GN]
    print('Getting GN data... [' + gn_target + ']')
    gn_command = base_command + [gn_build_dir, gn_target]
    result = subprocess.check_output(gn_command, cwd=source_dir)
    gn_data = json.loads(result)

    zn_target = TARGETS[args.type][Build.ZN]
    print('Getting ZN data... [' + zn_target + ']')
    zircon_dir = os.path.join(source_dir, 'zircon')
    zn_command = base_command + ['--root=' + zircon_dir, zn_build_dir, zn_target]
    result = subprocess.check_output(zn_command, cwd=source_dir)
    zn_data = json.loads(result)

    gn_object = gn_data.items()[0][1];
    zn_object = zn_data.items()[0][1];

    for dimension in DIMENSIONS:
        diff_lists(gn_object, zn_object, dimension);

    return 0


if __name__ == '__main__':
    exit(main())
