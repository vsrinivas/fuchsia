#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import component_manifest
import json
import os
import paths
import re
import sys
import urlparse

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

    def __init__(self):
        self.packages = []
        self.deps = []
        self.labels = []
        self.config_paths = []
        self.component_urls = []
        self.build_root = ""
        self.boot = Filesystem() # Files that will live in /boot
        self.system = Filesystem() # Files that will live in /system
        self.resources = []
        self.gopaths = []


    def add_config(self, config, config_path):
        self.config_paths.append(config_path)
        packages = config.get("packages", {})
        for package in packages:
            if package in self.packages:
                raise Exception("Duplicate package name: %s" % package)
            self.packages.append(package)
            self.deps.append(packages[package])

        for c in config.get("components", []):
            # See https://fuchsia.googlesource.com/modular/src/component_manager/ for what a component is.
            manifest = component_manifest.ComponentManifest(os.path.join(paths.FUCHSIA_ROOT, c))
            self.component_urls.append(manifest.url)
            for component_file in manifest.files().values():
                self.system.add_file({
                    'file': os.path.join(self.build_root, 'components', component_file.url_as_path),
                    'bootfs_path': os.path.join('components', component_file.url_as_path),
                    'default': False
                })
        # TODO(jamesr): Everything below here is deprecated and no longer needed
        # when all package configurations are written in GN template. Migrate
        # and remove.
        for label in config.get("labels", []):
            self.deps.append(label)
        binaries_and_drivers = config.get("binaries", []) + config.get("drivers", [])
        for b in binaries_and_drivers:
            file = {}
            file["file"] = os.path.join(self.build_root, b["binary"])
            file["bootfs_path"] = b["bootfs_path"]
            file["default"] = b.has_key("default")
            self.system.add_file(file)
        for d in config.get("early_boot", []):
            file = {}
            file["file"] = os.path.join(self.build_root, d["binary"])
            file["bootfs_path"] = d["bootfs_path"]
            file["default"] = d.has_key("default")
            self.boot.add_file(file)
        for r in config.get("resources", []):
            file = {}
            source_path = os.path.join(paths.FUCHSIA_ROOT, r["file"])
            file["file"] = source_path
            self.resources.append(source_path)
            file["bootfs_path"] = r["bootfs_path"]
            file["default"] = r.has_key("default")
            self.system.add_file(file)
        if config.get("gopaths"):
            self.gopaths.extend(config.get("gopaths"))

def detect_duplicate_keys(pairs):
    keys = set()
    result = {}
    for k, v in pairs:
        if k in keys:
            raise Exception("Duplicate key %s" % k)
        keys.add(k)
        result[k] = v
    return result


def resolve_imports(import_queue, build_root):
    # Hack: Add cpp manifest until we derive runtime information from the
    # package configs themselves.
    import_queue.append('packages/gn/cpp')
    imported = set(import_queue)
    amalgamation = Amalgamation()
    amalgamation.build_root = build_root
    while import_queue:
        config_name = import_queue.pop()
        config_path = os.path.join(paths.FUCHSIA_ROOT, config_name)
        with open(config_path) as f:
            try:
                config = json.load(f, object_pairs_hook=detect_duplicate_keys)
                amalgamation.add_config(config, config_path)
                for i in config.get("imports", []):
                    if i not in imported:
                        import_queue.append(i)
                        imported.add(i)
            except Exception as e:
                import traceback
                traceback.print_exc()
                sys.stderr.write("Failed to parse config %s, error %s\n" % (config_path, str(e)))
                return None
    return amalgamation


def update_gopath(maps, build_root):
    for gopath in maps:
        for src in gopath:
            target = os.path.join(build_root, "src", gopath[src])
            src = os.path.join(paths.FUCHSIA_ROOT, src)
            if not os.path.exists(os.path.dirname(target)):
                os.makedirs(os.path.dirname(target))
            if os.path.lexists(target):
                os.remove(target)
            os.symlink(src, target)

def write_manifest(manifest, files, autorun=None):
    manifest_dir = os.path.dirname(manifest)
    if not os.path.exists(manifest_dir):
        os.makedirs(manifest_dir)
    with open(manifest, "w") as manifest_file:
        for f in files:
            manifest_file.write("%s=%s\n" % (f["bootfs_path"], f["file"]))
        if autorun:
            manifest_file.write("autorun=%s\n" % autorun)


def main():
    parser = argparse.ArgumentParser(description="Generate bootfs manifest and "
                                     + "list of GN targets for a list of Fuchsia modules")
    parser.add_argument("--packages", help="path to packages file to generate")
    parser.add_argument("--boot-manifest", help="path to manifest file to generate for /boot")
    parser.add_argument("--system-manifest", help="path to manifest file to generate for /system")
    parser.add_argument("--modules", help="list of modules", default="default")
    parser.add_argument("--omit-files", help="list of files omitted from user.bootfs", default="")
    parser.add_argument("--autorun", help="path to autorun script", default="")
    parser.add_argument("--build-root", help="path to root of build directory")
    parser.add_argument("--depfile", help="path to depfile to generate")
    parser.add_argument("--component-index", help="path to component index to generate")
    parser.add_argument("--arch", help="architecture being targetted")
    args = parser.parse_args()

    amalgamation = resolve_imports(args.modules.split(","), args.build_root)
    if not amalgamation:
        return 1

    write_manifest(args.boot_manifest, amalgamation.boot.files)
    write_manifest(args.system_manifest, amalgamation.system.files, autorun=args.autorun)

    update_gopath(amalgamation.gopaths, amalgamation.build_root)

    if args.depfile != "":
        with open(args.depfile, "w") as f:
            f.write("user.bootfs: ")
            f.write(args.boot_manifest + " " + args.system_manifest)
            for path in amalgamation.config_paths:
                f.write(" " + path)
            for resource in amalgamation.resources:
                f.write(" " + resource)

    if args.component_index != "":
        with open(args.component_index, "w") as f:
            json.dump(amalgamation.component_urls, f)

    with open(args.packages, "w") as f:
        f.write("\n".join(amalgamation.packages))
    sys.stdout.write("\n".join(amalgamation.deps))
    sys.stdout.write("\n")
    return 0

if __name__ == "__main__":
    sys.exit(main())
