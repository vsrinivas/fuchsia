# vim: set expandtab:ts=2:sw=2
# Copyright (c) 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Autocompletion config for YouCompleteMe in Fuchsia

import os
import re
import stat
import subprocess

# NOTE: paths.py is a direct copy from //packages/gn/paths.py
# If there is an issue with the paths not being valid, just pull a new copy.
import sys
sys.path.append(os.path.dirname(__file__))
import paths as fuchsia_paths

fuchsia_root = os.path.realpath(fuchsia_paths.FUCHSIA_ROOT)
fuchsia_build = subprocess.check_output(
    [os.path.join(fuchsia_paths.FUCHSIA_ROOT, 'scripts/fx'), 'get-build-dir']).strip()

fuchsia_clang = os.path.join(fuchsia_paths.BUILDTOOLS_PATH, 'clang')
ninja_path = os.path.join(fuchsia_root, 'buildtools', 'ninja')

# Get the name of the zircon project from GN args.
# Reading the args.gn is significantly faster than running `gn args` so we do
# that.
zircon_project = None
args = open(os.path.join(fuchsia_build, 'args.gn')).read()
match = re.search(r'zircon_project\s*=\s*"([^"]+)"', args)
if match:
    zircon_project = match.groups()[0]

common_flags = [
    '-std=c++14',
    '-xc++',
    '-isystem',
    fuchsia_clang + '/include',
    '-isystem',
    fuchsia_clang + '/include/c++/v1',
]

# Add the sysroot include if we found the zircon project
if zircon_project:
    common_flags += ['-I' + os.path.join(
        fuchsia_root, 'out/build-zircon', 'build-' + zircon_project, 'sysroot/include')]

default_flags = [
    '-I' + fuchsia_root,
    '-I' + fuchsia_build + '/gen'
]


def GetClangCommandFromNinjaForFilename(filename):
  """Returns the command line to build |filename|.

  Asks ninja how it would build the source file. If the specified file is a
  header, tries to find its companion source file first.

  Args:
    filename: (String) Path to source file being edited.

  Returns:
    (List of Strings) Command line arguments for clang.
  """

  fuchsia_flags = []

  # Header files can't be built. Instead, try to match a header file to its
  # corresponding source file.
  if filename.endswith('.h'):
    alternates = ['.cc', '.cpp', '_unittest.cc']
    for alt_extension in alternates:
      alt_name = filename[:-2] + alt_extension
      if os.path.exists(alt_name):
        filename = alt_name
        break
    else:
      # If this is a standalone .h file with no source, the best we can do is
      # try to use the default flags.
      return fuchsia_flags

  # Ninja needs the path to the source file from the output build directory.
  # Cut off the common part and /. Also ensure that paths are real and don't
  # contain symlinks that throw the len() calculation off.
  filename = os.path.realpath(filename)
  subdir_filename = filename[len(fuchsia_root) + 1:]
  rel_filename = os.path.join('..', '..', subdir_filename)

  # Ask ninja how it would build our source file.
  ninja_command = [
      ninja_path, '-v', '-C', fuchsia_build, '-t', 'commands',
      rel_filename + '^'
  ]
  p = subprocess.Popen(ninja_command, stdout=subprocess.PIPE)
  stdout, stderr = p.communicate()
  if p.returncode:
    return fuchsia_flags

  # Ninja might execute several commands to build something. We want the last
  # clang command.
  clang_line = None
  for line in reversed(stdout.split('\n')):
    if 'clang' in line:
      clang_line = line
      break
  else:
    return default_flags + fuchsia_flags

  # Parse out the -I and -D flags. These seem to be the only ones that are
  # important for YCM's purposes.
  for flag in clang_line.split(' '):
    if flag.startswith('-I'):
      # Relative paths need to be resolved, because they're relative to the
      # output dir, not the source.
      if flag[2] == '/':
        fuchsia_flags.append(flag)
      else:
        abs_path = os.path.normpath(os.path.join(fuchsia_build, flag[2:]))
        fuchsia_flags.append('-I' + abs_path)
    elif ((flag.startswith('-') and flag[1] in 'DWFfmO') or
          flag.startswith('-std=') or flag.startswith('--target=') or
          flag.startswith('--sysroot=')):
      fuchsia_flags.append(flag)
    else:
      print('Ignoring flag: %s' % flag)

  return fuchsia_flags


def FlagsForFile(filename):
  """This is the main entry point for YCM. Its interface is fixed.

  Args:
    filename: (String) Path to source file being edited.

  Returns:
    (Dictionary)
      'flags': (List of Strings) Command line flags.
      'do_cache': (Boolean) True if the result should be cached.
  """
  fuchsia_flags = GetClangCommandFromNinjaForFilename(filename)
  final_flags = common_flags + fuchsia_flags

  return {'flags': final_flags, 'do_cache': True}
