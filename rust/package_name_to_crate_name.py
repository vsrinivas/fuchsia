#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

def main():
  parser = argparse.ArgumentParser(
      description='Converts a library package name to a Rust crate name')
  parser.add_argument('--package-name',
                      help='Name of the package',
                      required=True)
  args = parser.parse_args()
  print(args.package_name.replace('-', '_'))


if __name__ == '__main__':
  sys.exit(main())
