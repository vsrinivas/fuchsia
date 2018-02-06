#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import paths
import re
import sys
import urlparse

from package_imports_resolver import PackageImportsResolver


class Filesystem:
    def __init__(self):
        self.files = []
        self.paths = {}

    def add_file(self, file):
        bootfs_path = file["bootfs_path"]
        if self.paths.has_key(bootfs_path):
            old_entry = self.paths[bootfs_path]
            if file["default"] and not old_entry["default"]:
                return  # we don't override a non-default with a default value
            if not old_entry["default"]:
                raise Exception('Duplicate bootfs path %s' % bootfs_path)
        self.files.append(file)
        self.paths[bootfs_path] = file


class Amalgamation:

    def __init__(self, build_root):
        self.packages = []
        self.deps = []
        self.config_paths = []
        self.component_urls = []
        self.build_root = build_root
        self.system = Filesystem() # Files that will live in /system
        self.resources = []

    def import_resolved(self, config, config_path):
        self.config_paths.append(config_path)
        packages = config.get("packages", {})
        for package in packages:
            if package in self.packages:
                raise Exception("Duplicate package name: %s" % package)
            self.packages.append(package)
            self.deps.append(packages[package])

        # TODO(jamesr): Everything below here is deprecated and no longer needed
        # when all package configurations are written in GN template. Migrate
        # and remove.
        for r in config.get("resources", []):
            file = {}
            source_path = os.path.join(paths.FUCHSIA_ROOT, r["file"])
            file["file"] = source_path
            self.resources.append(source_path)
            file["bootfs_path"] = r["bootfs_path"]
            file["default"] = r.has_key("default")
            self.system.add_file(file)
        for key in ["binaries", "components", "drivers", "gopaths"]:
            if config.has_key(key):
                raise Exception("The \"%s\" key is no longer supported" % key)


def manifest_contents(files):
    return ''.join("%s=%s\n" % (f["bootfs_path"], f["file"]) for f in files)


def update_file(file, contents):
    if os.path.exists(file) and os.path.getsize(file) == len(contents):
        with open(file, 'r') as f:
            if f.read() == contents:
                return
    dir = os.path.dirname(file)
    if not os.path.exists(dir):
        os.makedirs(dir)
    with open(file, 'w') as f:
        f.write(contents)


def main():
    parser = argparse.ArgumentParser(description="Generate bootfs manifest and "
                                     + "list of GN targets for a list of "
                                     + "Fuchsia packages")
    parser.add_argument("--targets-file",
                        help="path to the GN targets file to generate")
    parser.add_argument("--system-manifest",
                        help="path to manifest file to generate for /system")
    parser.add_argument("--packages", help="list of packages",
                        default="default")
    parser.add_argument("--omit-files",
                        help="list of files omitted from user.bootfs",
                        default="")
    parser.add_argument("--build-root", help="path to root of build directory")
    parser.add_argument("--depfile", help="path to depfile to generate")
    args = parser.parse_args()

    amalgamation = Amalgamation(args.build_root)
    imports_resolver = PackageImportsResolver(amalgamation)
    imported = imports_resolver.resolve_imports(args.packages.split(","))
    if not imported:
        return 1

    system_manifest_contents = manifest_contents(amalgamation.system.files)
    update_file(args.system_manifest, system_manifest_contents)

    if args.depfile:
        with open(args.depfile, "w") as f:
            f.write("user.bootfs: %s" % args.system_manifest)
            for path in amalgamation.config_paths:
                f.write(" " + path)
            for resource in amalgamation.resources:
                f.write(" " + resource)

    update_file(args.targets_file, '\n'.join(amalgamation.packages) + '\n')

    sys.stdout.write("\n".join(amalgamation.deps))
    sys.stdout.write("\n")
    return 0

if __name__ == "__main__":
    sys.exit(main())
