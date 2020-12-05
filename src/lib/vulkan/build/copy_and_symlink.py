#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

description = """
Copies a file, and then makes a symlink to the copy.
"""
usage = "%prog source copy_to link_to"
epilog = """
A normal copy (not a hardlink) is made from source -> copy_to.
A soft link is made from copy_to -> link_to, assuming that they're
in the same directory.

This script will not work on Windows.
"""

import errno
import optparse
import os.path
import shutil
import subprocess
import sys


def Main(argv):
  parser = optparse.OptionParser(usage=usage, description=description,
                                 epilog=epilog)
  options, args = parser.parse_args(argv[1:])
  if len(args) != 3:
    parser.error('exactly 3 arguments required')

  if os.name == 'nt':
    print('Windows not supported.', file=sys.stderr)
    return 1

  source, copy_to, link_to = args

  if os.path.dirname(copy_to) != os.path.dirname(link_to):
    print('copy_to and link_to expected to be in the same directory.',
          file=sys.stderr)
    return 1

  shutil.copyfile(source, copy_to)
  if os.path.isfile(link_to):
    os.remove(link_to)
  os.symlink(os.path.split(copy_to)[1], link_to)
  return 0

if __name__ == '__main__':
  sys.exit(Main(sys.argv))
