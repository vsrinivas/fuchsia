#!/usr/bin/env python2.7
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import string
import sys


def parse_dot_packages(dot_packages_path):
    dot_packages_path = os.path.abspath(dot_packages_path)
    deps = {}
    with open(dot_packages_path) as dot_packages:
        for line in dot_packages:
            if line.startswith('#'):
                continue
            delim = line.find(':')
            if delim == -1:
                continue
            name = line[:delim]
            path = os.path.normpath(
                os.path.join(
                    os.path.dirname(dot_packages_path),
                    line[delim + 1:].strip()))
            if name in deps:
                raise Exception(
                    '%s contains multiple entries for package %s' %
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
    parser.add_argument(
        "--out", help="Path to .packages file to generate", required=True)
    parser.add_argument(
        "--package-name", help="Name of this package", required=True)
    parser.add_argument(
        "--source-dir", help="Path to package source", required=True)
    parser.add_argument(
        "--deps", help="List of dependencies' package file", nargs="*")
    args = parser.parse_args()

    dot_packages_file = args.out
    create_base_directory(dot_packages_file)

    package_deps = {
        args.package_name: os.path.abspath(args.source_dir),
    }

    for dep in args.deps:
        dependent_packages = parse_dot_packages(dep)
        for name, path in dependent_packages.iteritems():
            if name in package_deps:
                if path != package_deps[name]:
                    print "Error, conflicting entries for %s: %s and %s from %s" % (
                        name, path, package_deps[name], dep)
                    return 1
            else:
                package_deps[name] = path

    dot_packages_file = os.path.abspath(dot_packages_file)
    with open(dot_packages_file, "w") as dot_packages:
        names = package_deps.keys()
        names.sort()
        for name in names:
            dot_packages.write(
                '%s:%s/\n' % (
                    name,
                    os.path.relpath(
                        package_deps[name],
                        os.path.dirname(dot_packages_file))))

    return 0


if __name__ == '__main__':
    sys.exit(main())
