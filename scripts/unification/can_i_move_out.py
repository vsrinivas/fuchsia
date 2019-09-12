#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser('Determines whether libraries can be '
                                     'moved out of the ZN build')
    parser.add_argument('--source-dir',
                        help='Path to the root of the source tree',
                        default='.')
    parser.add_argument('--build-dir',
                        help='Path to the GN build dir',
                        required=True)
    type = parser.add_mutually_exclusive_group(required=True)
    type.add_argument('--banjo',
                      help='The target is a Banjo library',
                      action='store_true')
    type.add_argument('--fidl',
                      help='The target is a FIDL library',
                      action='store_true')
    type.add_argument('--ulib',
                      help='The target is a C/C++ library',
                      action='store_true')
    parser.add_argument('name',
                        help='Name of the library to inspect',
                        nargs=1)
    args = parser.parse_args()

    source_dir = os.path.abspath(args.source_dir)
    zircon_dir = os.path.join(source_dir, 'zircon')
    build_dir = os.path.abspath(args.build_dir)

    if sys.platform.startswith('linux'):
        platform = 'linux-x64'
    elif sys.platform.startswith('darwin'):
        platform = 'mac-x64'
    else:
        print('Unsupported platform: %s' % sys.platform)
        return 1
    gn_binary = os.path.join(source_dir, 'prebuilt', 'third_party', 'gn',
                             platform, 'gn')

    name = args.name[0]
    if args.fidl:
        type = 'fidl'
        name = name.replace('.', '-')
    elif args.banjo:
        type = 'banjo'
    elif args.ulib:
        type = 'ulib'
    category_label = '//system/' + type
    base_label = '//system/' + type + '/' + name

    gn_command = [gn_binary, '--root=' + zircon_dir, 'refs', build_dir,
                  base_label + ':*', '--all-toolchains']
    output = subprocess.check_output(gn_command)

    references = set()
    for line in output.splitlines():
        line = line.strip()
        if line.startswith(base_label):
            continue
        # Remove target name and toolchain.
        line = line[0:line.find(':')]
        if line == category_label:
            continue
        # Insert 'zircon' directory at the start.
        line = '//zircon' + line[1:]
        references.add(line)

    if references:
        print('Nope, there are still references in the ZN build:')
        for ref in sorted(references):
            print('  ' + ref)
    else:
        print('Yes you can!')

    return 0


if __name__ == '__main__':
    sys.exit(main())
