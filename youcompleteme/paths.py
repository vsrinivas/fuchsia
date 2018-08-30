#!/usr/bin/env python
# vim: set expandtab:ts=2:sw=2
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import platform
import re
import sys

SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
FUCHSIA_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, os.pardir, os.pardir))
GN_PATH = os.path.join(FUCHSIA_ROOT, 'buildtools', 'gn')
MKBOOTFS_PATH = os.path.join(FUCHSIA_ROOT, 'out', 'build-zircon', 'tools', 'mkbootfs')
BUILDTOOLS_PATH = os.path.join(FUCHSIA_ROOT, 'buildtools', '%s-%s' % (
    platform.system().lower().replace('darwin', 'mac'),
    {
        'x86_64': 'x64',
        'aarch64': 'arm64',
    }[platform.machine()],
))
DEBUG_OUT_DIR = os.path.join(FUCHSIA_ROOT, 'out', 'debug-x64')
RELEASE_OUT_DIR = os.path.join(FUCHSIA_ROOT, 'out', 'release-x64')

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
  clang_lib_path = recursive_search(root, 'clang/lib/clang')
  if not clang_lib_path:
    print('Could not find clang installation')
    return None
  # Now that we have the clang lib location, we need to find where the
  # actual include files are.
  installation_path = recursive_search(clang_lib_path, 'include')
  # recursive_search returns the include path, so we need to remove it.
  return os.path.dirname(installation_path)

# We start seaching from the correct buildtools.
CLANG_PATH = search_clang_path(BUILDTOOLS_PATH)

_BUILD_TOOLS = {}

def build_tool(package, tool):
  """Return the full path of TOOL binary in PACKAGE.

  This function memoizes its results, so there's not much need to
  cache its results in calling code.

  Raises:
    AssertionError: if the binary doesn't exist.
  """

  path = _BUILD_TOOLS.get((package, tool))
  if path is None:
    path = os.path.join(BUILDTOOLS_PATH, package, 'bin', tool)
    assert os.path.exists(path), 'No "%s" tool in "%s"' % (tool, package)
    _BUILD_TOOLS[package, tool] = path
  return path

def main():
  variable_re = re.compile('^[A-Z][A-Z_]*$')
  def usage():
    print 'Usage: path.py VARIABLE'
    print 'Available variables:'
    print '\n'.join(filter(variable_re.match, globals().keys()))
  if len(sys.argv) != 2:
    usage()
    return
  variable = sys.argv[1]
  if not variable_re.match(variable) or variable not in globals().keys():
    usage()
    return
  print globals()[variable]

if __name__ == '__main__':
  main()
