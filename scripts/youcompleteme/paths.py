#!/usr/bin/env python2.7
# vim: set expandtab:ts=2:sw=2
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import os
import platform
import re
import sys

SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
FUCHSIA_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, os.pardir, os.pardir))
GN_PATH = os.path.join(FUCHSIA_ROOT, 'buildtools', 'gn')
MKBOOTFS_PATH = os.path.join(FUCHSIA_ROOT, 'out', 'build-zircon', 'tools', 'mkbootfs')
DEBUG_OUT_DIR = os.path.join(FUCHSIA_ROOT, 'out', 'debug-x64')
RELEASE_OUT_DIR = os.path.join(FUCHSIA_ROOT, 'out', 'release-x64')
PREBUILT_PATH = os.path.join(FUCHSIA_ROOT, 'prebuilt')

def get_os():
    return platform.system().lower().replace('darwin', 'mac')

def get_arch():
    return {
        'x86_64': 'x64',
        'aarch64': 'arm64',
    }[platform.machine()]

# Returns a string with format "<OS>-<ARCH>" (eg. linux-x64).
def get_platform():
    return '%s-%s' % (get_os(), get_arch())

def recursive_search(root, pattern):
  """Looks for a particular directory pattern within a directory tree.

  Ignores files and git directories.

  Returns:
    the containing directory of the match or None if not found.
  """

  search_queue = [root]
  while search_queue:
    # List the children.
    current_path = search_queue.pop(0)
    for child in os.listdir(current_path):
      full_path = os.path.join(current_path, child)
      # Ignore files.
      if not os.path.isdir(full_path):
        continue
      # Ignore git.
      if child == '.git':
        continue
      # See if we found it.
      if pattern in full_path:
        return full_path
      # Otherwise, enqueue the path for searching.
      search_queue.append(full_path)
  return None

# Returns the base clang path. Note that this is not the clang/lib/clang but rather the base
# directory in out prebuilt directories.
def search_clang_path(root):
  """clang can change location, so we search where it landed.

  For now there is only one clang integration, so this should always find the
  correct one.
  This could potentially look over a number of directories, but this will only
  be run once at YCM server startup, so it should not affect overall
  performance.

  Returns:
    clang path or None.
  """

  # This is the root where we should search for the clang installation.
  clang_path = recursive_search(root, 'clang/%s' % get_platform())
  if not clang_path:
    print('Could not find clang installation')
    return None

  return clang_path

# We start seaching from the correct buildtools.
CLANG_PATH = search_clang_path(PREBUILT_PATH)

def main():
  variable_re = re.compile('^[A-Z][A-Z_]*$')
  def usage():
    print('Usage: path.py VARIABLE')
    print('Available variables:')
    print('\n'.join(filter(variable_re.match, globals().keys())))
  if len(sys.argv) != 2:
    usage()
    return
  variable = sys.argv[1]
  if not variable_re.match(variable) or variable not in globals().keys():
    usage()
    return
  print(globals()[variable])

if __name__ == '__main__':
  main()
