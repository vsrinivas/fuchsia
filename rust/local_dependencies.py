#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys
import re

ROOT_PATH = os.path.abspath(__file__ + "/../../..")
sys.path += [os.path.join(ROOT_PATH, "third_party", "pytoml")]
import pytoml

# The following paths are explicitly excluded as they probably do not contain
# valid BUILD.gn that provide labels for dependencies, and are assumed not to
# depend on fidl generated code.
EXCLUDED_PATHS = [
    "//third_party/rust-mirrors/"
]

def main():
    parser = argparse.ArgumentParser("Compute GN labels of all local dependencies")
    parser.add_argument("--target-toml",
                        help="Path to the Cargo.toml of the crate being checked",
                        required=True)
    parser.add_argument("--workspace-toml",
                        help="Path to the workspace Cargo.toml",
                        required=True)
    parser.add_argument("--workspace-lock",
                        help="Path to the workspace Cargo.lock",
                        required=True)
    parser.add_argument("--fuchsia-dir",
                        help="Path that represents // in GN",
                        required=True)
    args = parser.parse_args()

    with open(args.target_toml, "r") as file:
        target_toml = pytoml.load(file)

    with open(args.workspace_toml, "r") as file:
        workspace_toml = pytoml.load(file)

    with open(args.workspace_lock, "r") as file:
        workspace_lock = pytoml.load(file)

    workspace_root = os.path.dirname(args.workspace_toml)

    crate_name = target_toml["package"]["name"]

    lock_config = None
    for package in workspace_lock["package"]:
        if package["name"] == crate_name:
            lock_config = package
            break

    if not lock_config:
        sys.stderr.write("%s does not contain crate %s\n" % (args.workspace_lock, args.target_toml))
        return 1

    if not "dependencies" in lock_config:
        return 0

    patches = workspace_toml["patch"]["crates-io"]

    for dep in lock_config["dependencies"]:
        if re.search(r'\(registry\+', dep):
            continue
        dep_name = dep.split()[0]
        if not dep_name in patches:
            sys.stderr.write("%s is needs a patch declaration for %s\n"
                             % (args.workspace_toml, dep_name))
            return 1

        dep_path = os.path.join(workspace_root, patches[dep_name]["path"])
        gn_path = "//" + os.path.relpath(dep_path, args.fuchsia_dir)
        excluded = False
        for exclude in EXCLUDED_PATHS:
            if gn_path.startswith(exclude):
                excluded = True
                break
        if not excluded:
            gn_label = gn_path + ":" + dep_name + "_lib"
            print(gn_label)

    return 0


if __name__ == '__main__':
    sys.exit(main())

