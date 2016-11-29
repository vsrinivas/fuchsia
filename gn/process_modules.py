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

class Amalgamation:

    def __init__(self):
        self.labels = []
        self.files = []
        self.config_paths = []
        self.component_urls = []
        self.build_root = ""
        self.omit_files = []

    def add_config(self, config, config_path):
        self.config_paths.append(config_path)
        for label in config.get("labels", []):
            self.labels.append(label)
        for b in config.get("binaries", []):
            if b["binary"] in self.omit_files:
                continue
            file = {}
            file["file"] = os.path.join(self.build_root, b["binary"])
            file["bootfs_path"] = b["bootfs_path"]
            self.files.append(file)
        for r in config.get("resources", []):
            if r["file"] in self.omit_files:
                continue
            file = {}
            file["file"] = os.path.join(paths.FUCHSIA_ROOT, r["file"])
            file["bootfs_path"] = r["bootfs_path"]
            self.files.append(file)
        for c in config.get("components", []):
            # See https://fuchsia.googlesource.com/component_manager/ for what a component is.
            manifest = component_manifest.ComponentManifest(os.path.join(paths.FUCHSIA_ROOT, c))
            self.component_urls.append(manifest.url)
            for component_file in manifest.files().values():
                self.files.append({
                    'file': os.path.join(self.build_root, 'components', component_file.url_as_path),
                    'bootfs_path': os.path.join('components', component_file.url_as_path)
                })


def resolve_imports(import_queue, omit_files, build_root):
    imported = set(import_queue)
    amalgamation = Amalgamation()
    amalgamation.omit_files = omit_files
    amalgamation.build_root = build_root
    while import_queue:
        config_name = import_queue.pop()
        config_path = os.path.join(paths.SCRIPT_DIR, config_name)
        with open(config_path) as f:
            try:
                config = json.load(f)
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


def main():
    parser = argparse.ArgumentParser(description="Generate bootfs manifest and "
                                     + "list of GN targets for a list of Fuchsia modules")
    parser.add_argument("--manifest", help="path to manifest file to generate")
    parser.add_argument("--modules", help="list of modules", default="default")
    parser.add_argument("--omit-files", help="list of files omitted from user.bootfs", default="")
    parser.add_argument("--build-root", help="path to root of build directory")
    parser.add_argument("--depfile", help="path to depfile to generate")
    parser.add_argument("--component-index", help="path to component index to generate")
    parser.add_argument("--arch", help="architecture being targetted")
    args = parser.parse_args()

    amalgamation = resolve_imports(args.modules.split(","), args.omit_files.split(","), args.build_root)
    if not amalgamation:
        return 1

    mkbootfs_dir = os.path.join(paths.SCRIPT_DIR, "mkbootfs")
    manifest_dir = os.path.dirname(args.manifest)
    if not os.path.exists(manifest_dir):
        os.makedirs(manifest_dir)
    with open(os.path.join(args.manifest), "w") as manifest:
        manifest.write("user.bootfs:\n")
        libs = ["libc++abi.so.1",
                "libc++.so.2",
                "libunwind.so.1"]
        if args.arch == "x64":
            sysroot_arch_name = "x86_64-fuchsia"
        elif args.arch == "arm64":
            sysroot_arch_name = "aarch64-fuchsia"
        lib_root = os.path.join(
            paths.FUCHSIA_ROOT, "out", "sysroot", sysroot_arch_name, "lib")
        for lib in libs:
            manifest.write("lib/%s=%s\n" % (lib, os.path.join(lib_root, lib)))
        for file in amalgamation.files:
            manifest.write("%s=%s\n" % (file["bootfs_path"], file["file"]))
    if args.depfile != "":
        with open(args.depfile, "w") as f:
            f.write("user.bootfs: ")
            f.write(args.manifest)
            for path in amalgamation.config_paths:
                f.write(" " + path)

    if args.component_index != "":
        with open(args.component_index, "w") as f:
            json.dump(amalgamation.component_urls, f)

    sys.stdout.write("\n".join(amalgamation.labels))
    sys.stdout.write("\n")
    return 0

if __name__ == "__main__":
    sys.exit(main())
