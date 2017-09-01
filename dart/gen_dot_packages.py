#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import string
import sys

import label_to_package_name

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


def create_base_directory(file):
  path = os.path.dirname(file)
  if not os.path.exists(path):
    os.makedirs(path)


def main():
  parser = argparse.ArgumentParser(
      description="Generate .packages file for dart package")
  parser.add_argument("--out", help="Path to .packages file to generate",
                      required=True)
  parser.add_argument("--root-build-dir",
                      help="Path to root of the build directory", required=True)
  parser.add_argument("--root-gen-dir",
                      help="Path to root of the gen directory", required=True)
  parser.add_argument("--package-name", help="Name of this package")
  parser.add_argument("--package-label", help="Label of target for this package"
          "from which the package name is inferred")
  parser.add_argument("--source-dir", help="Path to package source",
                      required=True)
  parser.add_argument(
      "--depfile", help="Location of depfile to generate", required=True)
  parser.add_argument("--deps", help="List of dependencies", nargs="*")
  args = parser.parse_args()

  # Note: args.out will start with a toolchain subdirectory when applicable.
  # Not using args.root_gen_dir exclusively as depfile needs to use target paths
  # relative to args.root_build_dir.
  dot_packages_file = os.path.join(args.root_build_dir, args.out)
  create_base_directory(dot_packages_file)

  dependent_files = []

  package_deps = {}
  if args.package_name:
      package_name = args.package_name
  else:
      package_name = label_to_package_name.convert(args.package_label)
  package_deps[package_name] = args.source_dir
  for dep in args.deps:
    if not dep.startswith("//"):
      print "Error, expected dependency label to start with //"
      return 1
    # TODO(pylaligand): remove this workaround once Flutter is using the right
    # label.
    if dep == "//lib/widgets/packages/widgets:lib.widgets":
      dep = "//topaz/lib/widgets/packages/widgets:lib.widgets"
    target_base = dep[2:]
    target_sep = string.rfind(target_base, ":")
    if target_sep != -1:
      target_name = target_base[target_sep+1:]
      target_base = target_base[:target_sep]
    else:
      target_name = target_base[target_base.rfind("/")+1:]
    # Note: args.root_gen_dir takes the toolchain into account, so we know we
    # only depend on packages "built" for the same toolchain.
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
