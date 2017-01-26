#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import subprocess
import sys


DEFAULT_OUT_DIR='out/debug-x86-64'


def parse_package_file(package_file_path):
    with open(package_file_path) as package_file:
        data = json.load(package_file)
    return data


def netcp_everything(package_data, out_dir):
    for binary in package_data['binaries']:
        src_path = os.path.join(out_dir, binary['binary'])
        dst_path = os.path.join('/tmp', binary['bootfs_path'])

        print 'Copying "%s" to "%s"' % (src_path, dst_path)
        status = subprocess.call(['netcp', src_path, 'magenta:' + dst_path])
        if status != 0:
            print 'netcp failed'
            return status

    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("package_file",
                        help='JSON file containing package data')
    parser.add_argument('-o', '--out-dir', metavar='DIR',
                        help='Directory containing build products')
    args = parser.parse_args()

    package_data = parse_package_file(args.package_file)
    out_dir = args.out_dir or DEFAULT_OUT_DIR

    return netcp_everything(package_data, out_dir)


if __name__ == "__main__":
    sys.exit(main())
