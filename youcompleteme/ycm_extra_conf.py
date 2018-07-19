# vim: set expandtab:ts=2:sw=2
# Copyright (c) 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Autocompletion config for YouCompleteMe in Fuchsia

import os
import re
import stat
import subprocess
import ycm_core

# NOTE: paths.py is a direct copy from //build/gn/paths.py
# If there is an issue with the paths not being valid, just pull a new copy.
import sys
sys.path.append(os.path.dirname(os.path.realpath(__file__)))
import paths as fuchsia_paths

fuchsia_root = os.path.realpath(fuchsia_paths.FUCHSIA_ROOT)
zircon_database = None
zircon_dir = os.path.join(fuchsia_root, 'zircon')
# This doc explains how to generate compile_commands.json for Zircon:
# https://fuchsia.googlesource.com/zircon/+/HEAD/docs/editors.md
if os.path.exists(os.path.join(zircon_dir, 'compile_commands.json')):
  zircon_database = ycm_core.CompilationDatabase(zircon_dir)

os.chdir(fuchsia_root)
fuchsia_build = subprocess.check_output(
    [os.path.join(fuchsia_paths.FUCHSIA_ROOT, 'scripts/fx'),
     'get-build-dir']
    ).strip().decode('utf-8')

fuchsia_clang = os.path.join(fuchsia_paths.BUILDTOOLS_PATH, 'clang')
ninja_path = os.path.join(fuchsia_root, 'buildtools', 'ninja')

# Get the name of the zircon project from GN args.
# Reading the args.gn is significantly faster than running `gn args` so we do
# that.
target_cpu = None
args = open(os.path.join(fuchsia_build, 'args.gn')).read()
match = re.search(r'target_cpu\s*=\s*"([^"]+)"', args)
if match:
  target_cpu = match.groups()[0]

common_flags = [
    '-std=c++14',
    '-xc++',
    '-isystem',
    fuchsia_clang + '/include',
    '-isystem',
    fuchsia_clang + '/include/c++/v1',
    '-I' + fuchsia_root,
    '-I' + fuchsia_build + '/gen'
]

# Add the sysroot include if we found the zircon project
if target_cpu:
    arch_flags = ['-I' + os.path.join(
        fuchsia_root, 'out/build-zircon', 'build-' + target_cpu, 'sysroot/include')]


def GetClangCommandFromNinjaForFilename(filename):
  """Returns the command line to build |filename|.

  Asks ninja how it would build the source file. If the specified file is a
  header, tries to find its companion source file first.

  Args:
    filename: (String) Path to source file being edited.

  Returns:
    (List of Strings) Command line arguments for clang.
  """

  # By default we start with the common to every file flags
  fuchsia_flags = common_flags

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
    return fuchsia_flags

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
  if zircon_database and ('zircon/' in filename):
    zircon_compilation_info = zircon_database.GetCompilationInfoForFile(
      filename)
    if zircon_compilation_info.compiler_flags_:
      return {
        'flags': zircon_compilation_info.compiler_flags_,
        'include_paths_relative_to_dir':
            zircon_compilation_info.compiler_working_dir_,
        'do_cache': True
      }
  file_flags = GetClangCommandFromNinjaForFilename(filename)
  # We add the arch specific flags
  final_flags = file_flags + arch_flags

  return {'flags': final_flags, 'do_cache': True}
