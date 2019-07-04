#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

### Records the mapping from Rust package name to crate name and crate type

import argparse
import json
import os
import subprocess
import sys

import rust
from rust import ROOT_PATH

def deps_data_to_package_data_list(data_path):
    package_name_to_data = {}
    with open(data_path) as json_data:
        data = json.load(json_data)
    for package_name in data["crates"]:
        crate = data["crates"][package_name]
        ext = os.path.splitext(crate["lib_path"])[1]
        if ext == ".rlib":
            crate_type = "lib"
        elif ext == ".dylib" or ext == ".so":
            crate_type = "proc-macro"
        elif ext == "a":
            crate_type = "staticlib"
        else:
            print ("Unrecognized crate extension for " + package_name + ": " + ext)
            return None
        package_name_to_data[package_name] = {
            "crate_name": crate["crate_name"],
            "crate_type": crate_type,
        }

    # transform the data into a list since GN doesn't like dictionaries
    package_data_list = []
    for package_name, data in sorted(package_name_to_data.items()):
        package_data_list.append({
            "crate_name": data["crate_name"],
            "crate_type": data["crate_type"],
            "package_name": package_name,
        })

    return package_data_list

def main():
    parser = argparse.ArgumentParser(
            "Record mapping from Rust package name to crate name and crate type")
    parser.add_argument("--out-dir",
                        help="Path to the Fuchsia build directory",
                        required=False)
    parser.add_argument("--output",
                        help="Path to the output JSON file",
                        required=True)
    args = parser.parse_args()

    if args.out_dir:
        build_dir = args.out_dir
    else:
        build_dir = os.environ["FUCHSIA_BUILD_DIR"]

    fuchsia_data = os.path.join(build_dir, "rust_third_party_crates", "deps_data.json")
    host_data = os.path.join(build_dir, "host_x64", "rust_third_party_crates", "deps_data.json")

    fuchsia_package_data_list = deps_data_to_package_data_list(fuchsia_data)
    if fuchsia_package_data_list is None:
        return 1
    host_package_data_list = deps_data_to_package_data_list(host_data)
    if host_package_data_list is None:
        return 1

    out_data = {
        "fuchsia_packages": fuchsia_package_data_list,
        "host_packages": host_package_data_list,
    }

    with open(args.output, "w") as outfile:
        file.write(outfile, json.dumps(
            out_data,
            outfile,
            sort_keys=True,
            indent=4,
            separators=(",", ": "),
        ))

if __name__ == '__main__':
    sys.exit(main())

