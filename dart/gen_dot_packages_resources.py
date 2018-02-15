#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script translates a Dart .packages file into a manifest
# suitable for consumption by the "extra" parameter of the package() template
# in //build/package.gni.

import argparse
import os
import string
import subprocess
import sys
import urlparse

# Returns the set of files touched by gen_snapshot.
def GenSnapshotDeps(args):
  depfile_sources = None
  cmd = [
    args.gen_snapshot,
    '--print_dependencies',
    '--dependencies_only',
    '--packages=' + args.dot_packages,
    '--vm_snapshot_data=/dev/null',
    '--isolate_snapshot_data=/dev/null',
  ]
  for url_mapping in args.url_mapping:
    cmd.append("--url_mapping=" + url_mapping)
  cmd.append(args.main_dart)
  try:
    result = subprocess.check_output(cmd, stderr=subprocess.STDOUT)
  except subprocess.CalledProcessError as e:
    print ("gen_snapshot failed: " + ' '.join(cmd) + "\n" +
           "output: " + e.output)
    return 1
  if result:
    depfile_sources = set(result.strip().split('\n'))
  return depfile_sources


def WritePackageToManifest(manifest_file, dot_packages_file, depfile_sources,
                           package_name, package_path):
  path = urlparse.urlparse(package_path)
  path_len = len(path.path)
  manifest_lib_path = os.path.join('data', 'dart-pkg', package_name, 'lib')
  for root, dirs, files in os.walk(path.path):
    for f in files:
      # TODO(zra): What other files are needed?
      if not f.endswith('.dart'):
        continue
      full_path = os.path.join(root, f)
      # Only include files that are in depfile_sources
      if depfile_sources == None or full_path in depfile_sources:
        relative_path = full_path[path_len:]
        manifest_path = os.path.join(manifest_lib_path, relative_path)
        manifest_file.write(manifest_path + '=' + full_path + '\n')
  dot_packages_file.write(
      package_name + ':file:///pkg/' + manifest_lib_path + '/\n')


def WriteManifest(args, depfile_sources):
  with open(args.dot_packages) as dpfile, \
       open(args.manifest_out, 'w') as mfile, \
       open(args.dot_packages_out, 'w') as dpoutfile:
    for line in dpfile:
      package_and_uri = line.strip().split(':', 1)
      WritePackageToManifest(mfile, dpoutfile, depfile_sources,
                             package_and_uri[0], package_and_uri[1])
    mfile.write(os.path.join('data', 'dart-pkg', '.packages') + '=' +
                args.dot_packages_out + '\n')
    mfile.write(os.path.join('data', 'dart-pkg', 'contents') + '=' +
                args.contents_out + '\n')


def main():
  parser = argparse.ArgumentParser(
      description="Generate a manifest from a Dart .packages file")
  parser.add_argument("--contents-out",
      help="Output contents file to be included in the manifest",
      required=True)
  parser.add_argument("--dot-packages",
      help="Path to .packages file to translate",
      required=True)
  parser.add_argument("--dot-packages-out",
      help="Output .packages file to be included in the manifest",
      required=True)
  parser.add_argument("--gen-snapshot",
      help="Path to gen_snapshot",
      required=True)
  parser.add_argument("--main-dart",
      help="main.dart entrypoint of the program.",
      required=True)
  parser.add_argument("--manifest-out",
      help="Output manifest file.",
      required=True)
  parser.add_argument("--package",
      help="The name of the package containing the lib/main.dart entrypoint",
      required=True)
  parser.add_argument('--url-mapping',
      type=str,
      action='append',
      help='dart: url mappings to pass to gen_snapshot.')
  args = parser.parse_args()

  # Invoke gen_snapshot to get the list of .dart files used by the program.
  # This is used to filter out unused files from the package.
  depfile_sources = GenSnapshotDeps(args)

  # TODO(zra): At some point, it will likely make sense for the flutter content
  # handler to understand a metadata file describing the contents of the
  # package it needs to run. For now, we'll simply write the name of the
  # source package containing lib/main.dart, so that the content handler doesn't
  # have to guess it from the request url.
  with open(args.contents_out, 'w') as cfile:
    cfile.write(args.package)

  WriteManifest(args, depfile_sources)
  return 0


if __name__ == '__main__':
  sys.exit(main())
