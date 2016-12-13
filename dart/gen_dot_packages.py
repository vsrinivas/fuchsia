#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import string
import sys

def parse_dot_packages(dot_packages_path):
  deps = {}
  with open(dot_packages_path) as dot_packages:
      for line in dot_packages:
        if line.startswith('#'):
            continue
        delim = line.find(':file://')
        if delim == -1:
            continue
        name = line[:delim]
        path = os.path.abspath(line[delim + 8:].strip())
        if name in deps:
          raise Exception('%s contains multiple entries for package %s' %
              (dot_packages_path, name))
        deps[name] = path
  return deps




def main():
  parser = argparse.ArgumentParser(
      "Generate .packages file for dart package")
  parser.add_argument("--out", help="Path to .packages file to generate",
                      required=True)
  parser.add_argument("--root-build-dir",
                      help="Path to root of the build directory", required=True)
  parser.add_argument("--root-gen-dir",
                      help="Path to root of the gen directory", required=True)
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

  package_deps = {}
  package_deps[args.package_name] = args.source_dir
  for dep in args.deps:
    if not dep.startswith("//"):
      print "Error, expected dependency label to start with //"
      return 1
    target_base = dep[2:]
    target_sep = string.rfind(target_base, ":")
    if target_sep != -1:
      target_name = target_base[target_sep+1:]
      target_base = target_base[:target_sep]
    else:
      target_name = target_base[target_base.rfind("/")+1:]
    dep_dot_packages_path = os.path.join(
        args.root_gen_dir, target_base, "%s.packages" % target_name)
    dependent_files.append(dep_dot_packages_path)
    dependent_packages = parse_dot_packages(dep_dot_packages_path)
    for name, path in dependent_packages.iteritems():
      if name in package_deps:
        if path != package_deps[name]:
          print "Error, conflicting entries for %s: %s and %s from %s" % (name,
              path, package_deps[name], dep)
          return 1
      else:
        package_deps[name] = path

  with open(dot_packages_file, "w") as dot_packages:
    names = package_deps.keys()
    names.sort()
    for name in names:
      dot_packages.write('%s:file://%s/\n' % (name, package_deps[name]))

  with open(args.depfile, "w") as depfile:
    depfile.write("%s: %s\n" % (args.out, " ".join(dependent_files)))

  return 0

if __name__ == '__main__':
  sys.exit(main())
