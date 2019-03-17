#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys
import os


def main():
    parser = argparse.ArgumentParser('Copy host tests to host_tests')
    parser.add_argument('--json', help='a test.json-like file', action='append', required=True)
    parser.add_argument('--dest-dir', help='Where to copy the tests to', required=True)
    parser.add_argument('--stamp', help='File to touch when done', required=True)
    parser.add_argument('--depfile', help='Where to output dependency file', required=True)
    args = parser.parse_args()

    tests_json = []
    for test_json in args.json:
        with open(test_json, 'r') as f:
            tests_json.extend(json.load(f))

    try:
        os.mkdir(args.dest_dir)
    except OSError, e:
        pass

    files_read = []
    for spec in tests_json:
        if spec['test']['os'] == 'fuchsia':
            continue
        src = spec['test']['location']
        dst = os.path.join(args.dest_dir, os.path.basename(src))
        files_read.append(src)
        try:
            os.unlink(dst)
        except OSError, e:
            pass
        try:
            os.link(src, dst)
        except OSError, e:
            print('error: Failed to link src=%s dst=%s' % (src, dst))
            raise

    with open(args.depfile, 'w') as depfile:
        depfile.write('%s: ' % os.path.relpath(args.stamp, os.getcwd()))
        for path in files_read:
            depfile.write('%s ' % path)
        depfile.write('\n')

    with open(args.stamp, 'w') as stamp:
        stamp.write('Done!')
    return 0


if __name__ == '__main__':
    sys.exit(main())
