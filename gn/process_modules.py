#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys
import paths
import json

class Amalgamation:
    def __init__(self):
        self.labels = []
        self.binaries = []
        self.config_paths = []

    def add_config(self, config, config_path):
        self.config_paths.append(config_path)
        for label in config.get("labels", []):
            self.labels.append(label)
        for b in config.get("binaries", []):
            binary = {}
            binary["binary"] = b["binary"]
            binary["bootfs_path"] = b["bootfs_path"]
            self.binaries.append(binary)


def resolve_imports(import_queue):
    imported = set(import_queue)
    amalgamation = Amalgamation()
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
                print "Failed to parse config %s, error %s" % (config_path, str(e))
    return amalgamation


def main():
    parser = argparse.ArgumentParser(description="Generate bootfs manifest and"
            +"list of GN targets for a list of Fuchsia modules")
    parser.add_argument("--manifest", help="path to manifest file to generate")
    parser.add_argument("--modules", help="list of modules", default="default")
    parser.add_argument("--build-root", help="path to root of build directory")
    parser.add_argument("--depfile", help="path to depfile to generate")
    args = parser.parse_args()

    amalgamation = resolve_imports(args.modules.split(","))
    mkbootfs_dir = os.path.join(paths.SCRIPT_DIR, "mkbootfs")

    manifest_dir = os.path.dirname(args.manifest)
    if not os.path.exists(manifest_dir):
        os.makedirs(manifest_dir)
    with open(os.path.join(args.manifest), "w") as manifest:
        for binary in amalgamation.binaries:
            binary_path = os.path.join(args.build_root, binary["binary"])
            manifest.write("""%s=%s
""" % (binary["bootfs_path"], binary_path))

    if args.depfile != "":
        with open(args.depfile, "w") as f:
            f.write("user.bootfs:")
            for path in amalgamation.config_paths:
                f.write(" " + path)


    print "\n".join(amalgamation.labels)
    return 0

if __name__ == "__main__":
    sys.exit(main())
