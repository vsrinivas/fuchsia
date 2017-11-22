#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO(TO-471): Remove this script when the toolchain provides the manifest.

import argparse
import os
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, help="Output file")
    parser.add_argument("--target", required=True, help="Compiler target")
    parser.add_argument("--prefix", required=True, help="Toolchain prefix")
    parser.add_argument("soname", nargs='+', help="SONAME")
    args = parser.parse_args()

    cmd = [os.path.join(args.prefix, 'clang'),
           '-no-canonical-prefixes',
           '--target=' + args.target]

    contents = ''.join(sorted(
        'lib/%s=%s\n' % (name, subprocess.check_output(
            cmd + ['-print-file-name=' + name]).rstrip())
        for name in args.soname))

    if os.path.exists(args.output):
      with open(args.output, 'rb') as f:
        if f.read() == contents:
          return

    if not os.path.isdir(os.path.dirname(args.output)):
      os.makedirs(os.path.dirname(args.output))

    with open(args.output, 'wb') as f:
      f.write(contents)

if __name__ == '__main__':
    main()
