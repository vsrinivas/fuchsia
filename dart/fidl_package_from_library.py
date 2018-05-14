#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys


def main():
  parser = argparse.ArgumentParser(
      description='Converts a FIDL library name to a Dart package name')
  parser.add_argument('--name',
                      help='Name of the FIDL library',
                      required=True)
  args = parser.parse_args()
  print('fidl_%s' % args.name.replace('.', '_'))


if __name__ == '__main__':
  sys.exit(main())
