#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import string
import sys


def main():
  parser = argparse.ArgumentParser(
      "Generate .packages file for dart package")
  parser.add_argument("--out", help="Path to .packages file to generate",
                      required=True)
  parser.add_argument("--root-build-dir",
                      help="Path to root of the build directory", required=True)
  parser.add_argument("--package-name", help="Name of this package",
                      required=True)
  parser.add_argument("--source-dir", help="Path to package source",
                      required=True)
  parser.add_argument(
      "--depfile", help="Location of depfile to generate", required=True)
  parser.add_argument("--deps", help="List of dependencies", nargs="*")
  args = parser.parse_args()

  dot_packages_file = os.path.join(args.root_build_dir, args.out)
  dot_packages_path = os.path.dirname(dot_packages_file)
  if not os.path.exists(dot_packages_path):
    os.makedirs(dot_packages_path)

  dependent_files = []

  with open(dot_packages_file, "w") as dot_packages:
    dot_packages.write("%s:file://%s/\n" %
                       (args.package_name, args.source_dir))
    for dep in args.deps:
      if not dep.startswith("//"):
        print "Error, expected dependency label to start with //"
        return 1
      dep_dot_packages_path = os.path.join(
          args.root_build_dir, "gen", dep[2:], ".packages")
      dependent_files.append(dep_dot_packages_path)
      with open(dep_dot_packages_path) as dep_dot_packages:
        dot_packages.write(dep_dot_packages.read())

  with open(args.depfile, "w") as depfile:
    depfile.write("%s: %s\n" % (args.out, " ".join(dependent_files)))

  return 0

if __name__ == '__main__':
  sys.exit(main())
