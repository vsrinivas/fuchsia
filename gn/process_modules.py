#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import paths
import urlparse
import re
import sys

def component_url_bootfs_path(url):
    """Take an URL and generate a local path used by the component manager's fake network service"""
    return 'fake_network/' + re.sub(r'[:/]+', '/', url)


class Amalgamation:

    def __init__(self):
        self.labels = []
        self.files = []
        self.config_paths = []
        self.build_root = ""

    def add_config(self, config, config_path):
        self.config_paths.append(config_path)
        for label in config.get("labels", []):
            self.labels.append(label)
        for b in config.get("binaries", []):
            file = {}
            file["file"] = os.path.join(self.build_root, b["binary"])
            file["bootfs_path"] = b["bootfs_path"]
            self.files.append(file)
        for r in config.get("resources", []):
            file = {}
            file["file"] = os.path.join(paths.FUCHSIA_ROOT, r["file"])
            file["bootfs_path"] = r["bootfs_path"]
            self.files.append(file)
        for url, manifest_name in config.get("components", {}).items():
            # See https://fuchsia.googlesource.com/component_manager/ for what a component is.
            manifest_path = os.path.join(paths.FUCHSIA_ROOT, manifest_name)
            component_dir = os.path.dirname(manifest_path)
            manifest = json.load(open(manifest_path))
            # Add the manifest.
            self.files.append({
                "file": manifest_path,
                "bootfs_path": component_url_bootfs_path(url),
            })
            # Add the program.
            if 'fuchsia:program' in manifest:
                program_url = urlparse.urljoin(url, manifest['fuchsia:program']['url'])
                program_name = os.path.basename(urlparse.urlparse(program_url).path)
                self.files.append({
                    "file": os.path.join(self.build_root, program_name),
                    "bootfs_path": component_url_bootfs_path(program_url),
                })
            # Add the resources.
            for resource in manifest.get('fuchsia:resources', []):
                self.files.append({
                    "file": os.path.join(component_dir, resource),
                    "bootfs_path": component_url_bootfs_path(urlparse.urljoin(url, resource)),
                })



def resolve_imports(import_queue, build_root):
    imported = set(import_queue)
    amalgamation = Amalgamation()
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
                sys.stderr.write("Failed to parse config %s, error %s\n" % (config_path, str(e)))
                return None
    return amalgamation


def main():
    parser = argparse.ArgumentParser(description="Generate bootfs manifest and"
                                     + "list of GN targets for a list of Fuchsia modules")
    parser.add_argument("--manifest", help="path to manifest file to generate")
    parser.add_argument("--modules", help="list of modules", default="default")
    parser.add_argument("--build-root", help="path to root of build directory")
    parser.add_argument("--depfile", help="path to depfile to generate")
    parser.add_argument("--arch", help="architecture being targetted")
    args = parser.parse_args()

    amalgamation = resolve_imports(args.modules.split(","), args.build_root)
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

    sys.stdout.write("\n".join(amalgamation.labels))
    sys.stdout.write("\n")
    return 0

if __name__ == "__main__":
    sys.exit(main())
