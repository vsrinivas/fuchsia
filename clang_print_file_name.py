#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", default=None, help="Compiler target")
    parser.add_argument("--prefix", default="", help="Toolchain prefix")
    parser.add_argument("name", nargs='+', help="File name")
    args = parser.parse_args()

    cmd = [os.path.join(args.prefix, "clang")]
    if args.target:
        cmd.append("--target=" + args.target)

    for name in args.name:
        file = subprocess.check_output(cmd + ["-print-file-name=" + name])
        sys.stdout.write(file)

    return 0


if __name__ == '__main__':
    sys.exit(main())
