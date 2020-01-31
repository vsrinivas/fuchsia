#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--binaries-json', help='binaries.json file', required=True)
    parser.add_argument(
        '--root-build-dir', help='Root build directory', required=True)
    parser.add_argument(
        '--zircon-root-build-dir',
        help='Zircon root build directory',
        required=True)
    args = parser.parse_args()

    with open(args.binaries_json) as f:
        binaries = json.load(f)

    # Modify labels and paths to be valid in the Fuchsia build instead of the Zircon build.
    for binary in binaries:
        binary['label'] = binary['label'].replace('//', '//zircon/')
        for path_prop in ['dist', 'debug', 'elf_build_id', 'breakpad']:
            if path_prop in binary:
                binary[path_prop] = os.path.relpath(
                    os.path.join(args.zircon_root_build_dir, binary[path_prop]),
                    args.root_build_dir)

    json.dump(binaries, sys.stdout)

    return 0


if __name__ == '__main__':
    sys.exit(main())
