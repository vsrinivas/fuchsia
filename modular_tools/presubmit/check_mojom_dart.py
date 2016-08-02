#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Checks that released mojom.dart files in the source tree are up to date"""

import argparse
import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.dirname(SCRIPT_DIR)
THIRD_PARTY_DIR = os.path.join(SRC_DIR, 'third_party')
SDK_DIR = os.path.join(THIRD_PARTY_DIR, 'mojo', 'src', 'mojo', 'public')

# Insert path to mojom parsing library.
sys.path.insert(0, os.path.join(SDK_DIR,
                                'tools',
                                'bindings',
                                'pylib'))

from mojom.error import Error
from mojom.parse import parser_runner
from mojom.generate import mojom_translator

PACKAGE_MAP = {
    'modular' : os.path.join(SRC_DIR, 'public', 'dart'),
    'modular_services' : os.path.join(SRC_DIR, 'modular_services'),
}

# Script that calculates mojom output paths.
DART_OUTPUTS_SCRIPT = os.path.join(SRC_DIR,
                                   'tools',
                                   'bindings',
                                   'mojom_list_dart_outputs.py')

# Runs command line in args from cwd. Returns the output as a string.
def run(cwd, args):
  return subprocess.check_output(args, cwd=cwd)


# Given a mojom.Module, return the path of the .mojom.dart relative to its
# package directory.
def _mojom_output_path(mojom):
  name = mojom.name
  namespace = mojom.namespace
  elements = ['lib']
  elements.extend(namespace.split('.'))
  elements.append("%s.dart" % name)
  return os.path.join(*elements)


# Given a mojom.Module, return the package or None.
def _mojom_package(mojom):
  if mojom.attributes:
    return mojom.attributes.get('DartPackage')

# Load and parse a .mojom file. Returns the mojom.Module or raises an Exception
# if there was an error.
def _load_mojom(path_to_mojom):
  filename = os.path.abspath(path_to_mojom)
  mojom_file_graph = parser_runner.ParseToMojomFileGraph(SDK_DIR, [filename],
                                                         meta_data_only=True)
  if mojom_file_graph is None:
    raise Exception
  mojom_dict = mojom_translator.TranslateFileGraph(mojom_file_graph)
  return mojom_dict[filename]

def _print_regenerate_message(package):
  print("""
*** Dart Generated Bindings Check Failed for package: %s

To regenerate all bindings, from src directory, run:

modular gen
""" % (package))

# Returns a list of paths to .mojom files vended by package_name.
def _find_mojoms_for_package(package_name):
  # Run git grep for all .mojom files with DartPackage="package_name"
  try:
    output = run(SRC_DIR, ['git',
                           'grep',
                           '--name-only',
                           'DartPackage="' + package_name + '"',
                           '--',
                           '*.mojom'])
  except subprocess.CalledProcessError as e:
    # git grep exits with code 1 if nothing was found.
    if e.returncode == 1:
      return []

  # Process output
  mojoms = []
  for line in output.splitlines():
    line = line.strip()
    # Skip empty lines.
    if not line:
      continue
    mojoms.append(line)
  return mojoms


# Return the list of expected mojom.dart files for a package.
def _expected_mojom_darts_for_package(mojoms):
  output = run(SRC_DIR, ['python',
                         DART_OUTPUTS_SCRIPT,
                         '--mojoms'] + mojoms)
  mojom_darts = []
  for line in output.splitlines():
    line = line.strip()
    # Skip empty lines.
    if not line:
      continue
    mojom_darts.append(line)
  return mojom_darts


# Returns a map indexed by output mojom.dart name with the value of
# the modification time of the .mojom file in the source tree.
def _build_expected_map(mojoms, mojom_darts):
  assert(len(mojom_darts) == len(mojoms))
  expected = {}
  for i in range(0, len(mojoms)):
    mojom_path = os.path.join(SRC_DIR, mojoms[i])
    expected[mojom_darts[i]] = os.path.getmtime(mojom_path)
  return expected


# Returns a map indexed by output mojom.dart name with the value of
# the modification time of the .mojom.dart file in the source tree.
def _build_current_map(package):
  current = {}
  package_path = PACKAGE_MAP[package]
  for directory, _, files in os.walk(package_path):
    for filename in files:
      if filename.endswith('.mojom.dart'):
        path = os.path.abspath(os.path.join(directory, filename))
        relpath = os.path.relpath(path, start=SRC_DIR)
        current[relpath] = os.path.getmtime(path)
  return current


# Checks if a mojom.dart file we expected in the source tree isn't there.
def _check_new(package, expected, current):
  check_failure = False
  for mojom_dart in expected:
    if not current.get(mojom_dart):
      print("FAIL: Package %s missing %s" % (package, mojom_dart))
      check_failure = True
  return check_failure


# Checks if a mojom.dart file exists without an associated .mojom file.
def _check_delete(package, expected, current):
  check_failure = False
  for mojom_dart in current:
    if not expected.get(mojom_dart):
      print("FAIL: Package %s no longer has %s." % (package, mojom_dart))
      print("Delete %s", os.path.join(SRC_DIR, mojom_dart))
      check_failure = True
  return check_failure


# Checks if a .mojom.dart file is older than the associated .mojom file.
def _check_stale(package, expected, current):
  check_failure = False
  for mojom_dart in expected:
    # Missing mojom.dart file in source tree case handled by _check_new.
    source_mtime = expected[mojom_dart]
    if not current.get(mojom_dart):
      continue
    generated_mtime = current[mojom_dart]
    if generated_mtime < source_mtime:
      print("FAIL: Package %s has old %s" % (package, mojom_dart))
      check_failure = True
  return check_failure


# Checks that all .mojom.dart files are newer than time.
def _check_bindings_newer_than(package, current, time):
  for mojom_dart in current:
    if time > current[mojom_dart]:
      # Bindings are older than specified time.
      print("FAIL: Package %s has generated bindings older than the bindings"
            " scripts / templates." % package)
      return True
  return False


# Returns True if any checks fail.
def _check(package, expected, current, bindings_gen_mtime):
  check_failure = False
  if bindings_gen_mtime > 0:
    if _check_bindings_newer_than(package, current, bindings_gen_mtime):
      check_failure = True
  if _check_new(package, expected, current):
    check_failure = True
  if _check_stale(package, expected, current):
    check_failure = True
  if _check_delete(package, expected, current):
    check_failure = True
  return check_failure


def global_check(packages, bindings_gen_mtime=0):
  check_failure = False
  for package in packages:
    mojoms = _find_mojoms_for_package(package)
    if not mojoms:
      continue
    mojom_darts = _expected_mojom_darts_for_package(mojoms)
    # We only feed in mojom files with DartPackage annotations, therefore, we
    # should have a 1:1 mapping from mojoms[i] to mojom_darts[i].
    assert(len(mojom_darts) == len(mojoms))
    expected = _build_expected_map(mojoms, mojom_darts)
    current = _build_current_map(package)
    if _check(package, expected, current, bindings_gen_mtime):
      _print_regenerate_message(package)
      check_failure = True
  return check_failure


def is_mojom_dart(path):
  return path.endswith('.mojom.dart')


def is_mojom(path):
  return path.endswith('.mojom')


def filter_paths(paths, path_filter):
  result = []
  for path in paths:
    path = os.path.abspath(os.path.join(SRC_DIR, path))
    if path_filter(path):
      result.append(path)
  return result


def safe_mtime(path):
  try:
    return os.path.getmtime(path)
  except Exception:
    pass
  return 0


def is_bindings_machinery_path(filename):
  # NOTE: It's possible other paths inside of
  # mojo/public/tools/bindings/generators might also affect the Dart bindings.
  # The code below is somewhat conservative and may miss a change.
  # Dart templates changed.
  if filename.startswith(
        'mojo/public/tools/bindings/generators/dart_templates/'):
    return True
  # Dart generation script changed.
  if (filename ==
        'mojo/public/tools/bindings/generators/mojom_dart_generator.py'):
    return True
  return False


# Detects if any part of the Dart bindings generation machinery has changed.
def check_for_bindings_machinery_changes(affected_files):
  for filename in affected_files:
    if is_bindings_machinery_path(filename):
      return True
  return False


# Returns the latest modification time for any bindings generation
# machinery files.
def bindings_machinery_latest_mtime(affected_files):
  latest_mtime = 0
  for filename in affected_files:
    if is_bindings_machinery_path(filename):
      path = os.path.join(SRC_DIR, filename)
      mtime = safe_mtime(path)
      if mtime > latest_mtime:
        latest_mtime = mtime
  return latest_mtime


def presubmit_check(packages, affected_files):
  mojoms = filter_paths(affected_files, is_mojom)
  mojom_darts = filter_paths(affected_files, is_mojom_dart)

  if check_for_bindings_machinery_changes(affected_files):
    # Bindings machinery changed, perform global check instead.
    latest_mtime = bindings_machinery_latest_mtime(affected_files)
    return global_check(packages, latest_mtime)

  updated_mojom_dart_files = []
  deleted_mojom_files = []
  deleted_mojom_dart_files = []
  packages_with_failures = []
  check_failure = False

  # Check for updated .mojom without updated .mojom.dart
  for mojom_file in mojoms:
    if not os.path.exists(mojom_file):
      # File no longer exists. We cannot calculate the path of the associated
      # .mojom.dart file, skip.
      deleted_mojom_files.append(os.path.relpath(mojom_file, start=SRC_DIR))
      continue

    # ignore changes in third_party
    if mojom_file.startswith(THIRD_PARTY_DIR):
      continue

    try:
      mojom = _load_mojom(mojom_file)
    except Exception:
      # Could not load .mojom file
      print("Could not load mojom file: %s" % mojom_file)
      return True

    package = _mojom_package(mojom)
    # If a mojom doesn't have a package, ignore it.
    if not package:
     continue
    package_dir = packages.get(package)
    # If the package isn't a known package, ignore it.
    if not package_dir:
      check_failure = True
      print("Could not find package: %s. You might need to update the presumit "
            "package mapping at //modular_tools/presubmit/check_mojom_dart.py ."
            % package)
      continue
    # Expected output path relative to src.
    mojom_dart_path = os.path.relpath(
        os.path.join(package_dir, _mojom_output_path(mojom)), start=SRC_DIR)

    mojom_mtime = safe_mtime(mojom_file)
    mojom_dart_mtime = safe_mtime(os.path.join(SRC_DIR, mojom_dart_path))

    if mojom_mtime > mojom_dart_mtime:
      check_failure = True
      if mojom_dart_mtime == 0:
        print("Package %s is missing %s" % (package, mojom_dart_path))
      else:
        print("Package %s has old %s" % (package, mojom_dart_path))
      if not (package in packages_with_failures):
        packages_with_failures.append(package)
      continue

    # Remember that this .mojom.dart file was updated after the .mojom file.
    # This list is used to verify that all updated .mojom.dart files were
    # updated because their source .mojom file changed.
    updated_mojom_dart_files.append(mojom_dart_path)

  # Check for updated .mojom.dart file without updated .mojom file.
  for mojom_dart_file in mojom_darts:
    # mojom_dart_file is not inside //mojo/dart/packages.
    if not mojom_dart_file.startswith(SRC_DIR):
      continue

    # Path relative to //mojo/dart/packages/
    path_relative_to_packages = os.path.relpath(mojom_dart_file,
                                                start=SRC_DIR)
    # Package name is first element of split path.
    package = path_relative_to_packages.split(os.sep)[0]
    # Path relative to src.
    mojom_dart_path = os.path.relpath(mojom_dart_file, start=SRC_DIR)
    # If mojom_dart_path is not in updated_mojom_dart_files, a .mojom.dart
    # file was updated without updating the related .mojom file.
    if not (mojom_dart_path in updated_mojom_dart_files):
      if not os.path.exists(mojom_dart_file):
        deleted_mojom_dart_files.append(mojom_dart_path)
        continue
      check_failure = True
      print("Package %s has new %s without updating source .mojom file." %
            (package, mojom_dart_path))
      if not (package in packages_with_failures):
        packages_with_failures.append(package)

  for package in packages_with_failures:
    _print_regenerate_message(package)

  return check_failure


def main():
  parser = argparse.ArgumentParser(description='Generate a dart-pkg')
  parser.add_argument('--affected-files',
                      action='store',
                      metavar='affected_files',
                      help='List of files that should be checked.',
                      nargs='+')
  args = parser.parse_args()
  packages = PACKAGE_MAP

  # This script runs in two modes, the first mode is invoked by PRESUBMIT.py
  # and passes the list of affected files. This checks for the following cases:
  # 1) An updated .mojom file without an updated .mojom.dart file.
  # 2) An updated .mojom.dart file without an updated .mojom file.
  # NOTE: Case 1) also handles the case of a new .mojom file being added.
  #
  # The second mode does a global check of all packages under
  # //mojo/dart/packages. This checks for the following cases:
  # 1) An updated .mojom file without an updated .mojom.dart file.
  # 2) A .mojom.dart file without an associated .mojom file (deletion case).
  if args.affected_files:
    check_failure = presubmit_check(packages, args.affected_files)
  else:
    check_failure = global_check(packages)
  if check_failure:
    return 2
  return 0

if __name__ == '__main__':
    sys.exit(main())
