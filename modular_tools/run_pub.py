#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import hashlib
import os
import os.path
import subprocess
import sys

_SRC_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_LEDGER_ROOT = os.path.join(_SRC_ROOT, 'third_party', 'ledger')
_DART_SDK = os.path.join(_SRC_ROOT, 'third_party', 'flutter', 'bin', 'cache',
                         'dart-sdk', 'bin')


def _get_all_packages():
  packages = []
  excluded_dirs = set(['build', 'packages', 'third_party'])
  for root, dirs, files in os.walk(_SRC_ROOT, topdown=True):
    dirs[:] = [d for d in dirs if d not in excluded_dirs]
    for f in files:
      if f == 'pubspec.yaml':
        packages.append(root)

  packages.append(os.path.join(_SRC_ROOT, 'third_party', 'flutter', 'packages',
                               'flutter'))
  packages.append(_LEDGER_ROOT)
  return packages


def _get_files_to_hash(packages):
  """Computes a list of files to be hashed based on the list of packages in the
  tree.

  We need to hash both pubspec.yaml (to pick up changes made by the developer
  by hand) and pubspec.lock (to pick up changes made by others that are
  synced).
  """
  files_to_hash = []
  for package_path in packages:
    files_to_hash.append(os.path.join(package_path, 'pubspec.yaml'))
    files_to_hash.append(os.path.join(package_path, 'pubspec.lock'))
    files_to_hash.append(os.path.join(package_path, '.packages'))

  # Hash DEPS too as we depend on packages managed there.
  files_to_hash.append(os.path.join(_SRC_ROOT, 'DEPS'))
  return files_to_hash


def _ignore_yaml_comments(content):
  return '\n'.join([line for line in content.split('\n')
                    if not line.strip().startswith('#')])


def _hash(files_to_hash):
  """Computes a hash of all pubspec.yaml and pubspec.lock of the packages in the
  tree.
  """
  hasher = hashlib.sha1()
  for file_path in files_to_hash:
    if os.path.exists(file_path):
      with open(file_path, 'r') as file_to_hash:
        hasher.update(_ignore_yaml_comments(file_to_hash.read()))
  return hasher.hexdigest()


def _read_previous_hash():
  pubspec_hash_path = os.path.join(_SRC_ROOT, '.pubspec.hash')
  if not os.path.exists(pubspec_hash_path):
    return None
  with open(pubspec_hash_path, 'r') as pubspec_hash_file:
    return pubspec_hash_file.read()
  return None


def _write_pubspec_hash(pubspec_hash):
  pubspec_hash_path = os.path.join(_SRC_ROOT, '.pubspec.hash')
  with open(pubspec_hash_path, 'w') as pubspec_hash_file:
    pubspec_hash_file.write(pubspec_hash)


def _run_pub(package, command):
  fnull = open(os.devnull, 'w')
  succeeded = True

  print 'Running `pub %s` in %s... ' % (command, package),
  sys.stdout.flush()
  ret = subprocess.call([os.path.join(_DART_SDK, 'pub'), command], cwd=package,
                        stdout=fnull)
  if ret != 0:
    print 'ERROR'
    succeeded = False
  else:
    print 'done'
  sys.stdout.flush()
  return succeeded


def main():
  parser = argparse.ArgumentParser(description='Runs pub over all packages.')
  parser.add_argument('command', help='pub command to run.')
  parser.add_argument('--incremental', action='store_true',
                      help='Run only on packages with changes in pubspec.')
  parsed_args = parser.parse_known_args()
  args = parsed_args[0]
  all_packages = _get_all_packages()

  pubspec_hash = _hash(_get_files_to_hash(all_packages))
  if args.incremental:
    previous_pubspec_hash = _read_previous_hash()
    if pubspec_hash and pubspec_hash == previous_pubspec_hash:
      print ('No changes in pubspecs since last run, skipping the pub hook. '
             'You can do `modular_tools/run_pub.py get` to run it anyway.')
      sys.stdout.flush()
      return 0
    else:
      print 'At least one pubspec changed, cannot skip the pub hook.'
      sys.stdout.flush()

  all_succeeded = True
  for package in all_packages:
    if not _run_pub(package, args.command):
      all_succeeded = False

  if all_succeeded:
    # Recompute the hash, as .packages could have been changed by running pub.
    new_pubspec_hash = _hash(_get_files_to_hash(all_packages))
    _write_pubspec_hash(new_pubspec_hash)
    return 0

  return 1


if __name__ == '__main__':
  sys.exit(main())
