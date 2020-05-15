#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys

from common import FUCHSIA_ROOT


def main():
    parser = argparse.ArgumentParser(
            description='Lists paths involved in the ZN build')
    parser.add_argument('--build-dir',
                        help='Path to the ZN build\'s output directory',
                        default=os.path.join(FUCHSIA_ROOT, 'out', 'default.zircon'))
    args = parser.parse_args()

    with open(os.path.join(args.build_dir, 'build.ninja.d'), 'r') as ninja_file:
        content = ninja_file.readlines()[0]
    files = content.split()
    files = [f.replace('../../zircon', '/').replace('/BUILD.gn', '')
             for f in files if f.endswith('BUILD.gn')]
    for file in sorted(files):
        print(file)

    return 0


if __name__ == '__main__':
    sys.exit(main())
