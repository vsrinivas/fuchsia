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
import sys
import urlparse

def main():
  parser = argparse.ArgumentParser(
      description="Generate a manifest from a Dart .packages file")
  parser.add_argument("--dot-packages",
      help="Path to .packages file to translate",
      required=True)
  parser.add_argument("--dot-packages-out",
      help="Output .packages file for the package.",
      required=True)
  parser.add_argument("--manifest-out",
      help="Output manifest file.",
      required=True)
  args = parser.parse_args()

  with open(args.dot_packages) as dpfile, \
       open(args.manifest_out, 'w') as mfile, \
       open(args.dot_packages_out, 'w') as dpoutfile:
    for line in dpfile:
      package_and_uri = line.strip().split(':', 1)
      package_name = package_and_uri[0]
      path = urlparse.urlparse(package_and_uri[1])
      path_len = len(path.path)
      manifest_lib_path = os.path.join('data', package_name, 'lib')
      for root, dirs, files in os.walk(path.path):
        for f in files:
          # TODO(zra): What other files are needed?
          if not f.endswith('.dart'):
            continue
          full_path = os.path.join(root, f)
          relative_path = full_path[path_len:]
          manifest_path = os.path.join(manifest_lib_path, relative_path)
          mfile.write(manifest_path + '=' + full_path + '\n')
      dpoutfile.write(package_name + ':file:///pkg/' + manifest_lib_path + '/\n')
    mfile.write(os.path.join('data', '.packages') + '=' + args.dot_packages_out + '\n')
  return 0


if __name__ == '__main__':
  sys.exit(main())
