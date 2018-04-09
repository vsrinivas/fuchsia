#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import datetime
import json
import os
import subprocess
import sys

CARGO_TOML_CONTENTS = '''\
# Copyright %(year)s The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
[package]
name = "%(crate_name)s"
version = "%(version)s"
license = "BSD-3-Clause"
authors = ["rust-fuchsia@fuchsia.com"]
description = "Rust crate for Fuchsia OS"
repository = "https://fuchsia.googlesource.com"

[dependencies]
%(deps)s
'''

DEP_TEMPLATE = '%(crate_name)s = { version = "%(version)s", path = "%(path)s" }\n'

def cur_year():
    return datetime.datetime.now().year

def main():
    parser = argparse.ArgumentParser("Writes a Cargo.toml for a Rust crate")
    parser.add_argument("--crate-name",
                        help="Name of the crate",
                        required=True)
    parser.add_argument("--version",
                        help="Version of crate",
                        required=True)
    parser.add_argument("--out-dir",
                        help="Path to directory where Cargo.toml should be written",
                        required=True)
    parser.add_argument("--crate-type",
                        help="Type of crate to build",
                        required=True,
                        choices=["bin", "rlib", "staticlib", "proc-macro"])
    parser.add_argument("--third-party-deps-data",
                        help="Path to output of third_party_crates.py",
                        required=True)
    parser.add_argument("--dep-data",
                        action="append",
                        help="Path to metadata from a previous invocation of this script",
                        required=False)

    parser.add_argument
    args = parser.parse_args()
    cargo_toml_path = os.path.join(args.out_dir, "Cargo.toml")

    deps = ""
    third_party_json = json.load(open(args.third_party_deps_data))

    if args.dep_data:
        for data_path in args.dep_data:
            dep_data = json.load(open(data_path))
            if dep_data["third_party"]:
                crate = dep_data["crate_name"]
                crate_data = third_party_json["crates"][crate]
                deps += DEP_TEMPLATE % {
                    "crate_name": crate,
                    "path": crate_data["src_path"],
                    "version": crate_data["version"],
                }
            else:
                deps += DEP_TEMPLATE % {
                    "crate_name": dep_data["crate_name"],
                    "path": dep_data["src_path"],
                    "version": dep_data["version"],
                }

    with open(cargo_toml_path, "w") as file:
        file.write(CARGO_TOML_CONTENTS % {
            "crate_name": args.crate_name,
            "version": args.version,
            "deps": deps,
            "year": cur_year(),
        })

if __name__ == '__main__':
    sys.exit(main())
