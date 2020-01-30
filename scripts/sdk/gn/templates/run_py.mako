#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Runs the GN SDK tests
usage: run.py [-h] [--proj_dir PROJ_DIR] [--out_dir OUT_DIR]

optional arguments:
  -h, --help           show this help message and exit
  --proj_dir PROJ_DIR  Path to the test project directory
  --out_dir OUT_DIR    Path to the out directory

PROJ_DIR defaults to the same directory run.py is contained in
OUT_DIR defaults to ./out/default relative to the run.py
"""

import argparse
import os
from subprocess import check_output, Popen
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

ARCHES = [
    'arm64',
    'x64',
]

DEFAULT_OUT_DIR = os.path.join(SCRIPT_DIR, 'out')


def find_path(path):
    if os.path.exists(path):
        return os.path.realpath(path)
    return ''


class GnTester(object):
    """Class for GN SDK test setup, execution, and cleanup."""

    def __init__(self, proj_dir, arch, out_dir):
        self.proj_dir = proj_dir
        self.arch = arch
        self.out_dir = out_dir

    def run(self):
        return 0


def main():
    parser = argparse.ArgumentParser(description='Runs the GN SDK tests')
    parser.add_argument(
        '--proj_dir',
        help='Path to the test project directory',
        default=SCRIPT_DIR)
    parser.add_argument(
        '--out_dir', help='Path to the out directory', default=DEFAULT_OUT_DIR)
    args = parser.parse_args()

    proj_dir = find_path(args.proj_dir)
    if not proj_dir:
        print('"%s": path not found' % (args.proj_dir))
        return 1
    out_dir = os.path.realpath(args.out_dir)

    for arch in ARCHES:
        out = os.path.join(out_dir, arch)
        if GnTester(proj_dir, arch, out).run():
            return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
