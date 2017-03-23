#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import subprocess
import sys


DEFAULT_DST_ROOT='/system'
DEFAULT_OUT_DIR='out/debug-x86-64'


def parse_package_file(package_file_path):
    with open(package_file_path) as package_file:
        data = json.load(package_file)
    return data


def netcp_everything(package_data, out_dir, dst_root):
    for binary in package_data['binaries']:
        src_path = os.path.join(out_dir, binary['binary'])
        dst_path = os.path.join(dst_root, binary['bootfs_path'])

        print 'Copying "%s" to "%s"' % (src_path, dst_path)
        status = subprocess.call(['netcp', src_path, ':' + dst_path])
        if status != 0:
            print 'netcp failed'
            return status

    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("package_file",
                        help='JSON file containing package data')
    parser.add_argument('-o', '--out-dir', metavar='DIR',
                        default=DEFAULT_OUT_DIR,
                        help='Directory containing build products')
    parser.add_argument('-d', '--dst-root', metavar='PATH',
                        default=DEFAULT_DST_ROOT,
                        help='Path to the directory to copy package products')
    args = parser.parse_args()

    package_data = parse_package_file(args.package_file)
    out_dir = args.out_dir or DEFAULT_OUT_DIR
    dst_root = args.dst_root or DEFAULT_DST_ROOT

    return netcp_everything(package_data, out_dir, dst_root)


if __name__ == "__main__":
    sys.exit(main())
