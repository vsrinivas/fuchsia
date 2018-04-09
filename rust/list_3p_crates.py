#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys

ROOT_PATH = os.path.abspath(__file__ + "/../../..")
sys.path += [os.path.join(ROOT_PATH, "third_party", "pytoml")]
import pytoml

def main():
    parser = argparse.ArgumentParser(
            "Lists all of the names of crates included in a Cargo.toml file")
    parser.add_argument("--cargo-toml",
                        help="Path to Cargo.toml",
                        required=True)
    args = parser.parse_args()

    with open(args.cargo_toml, "r") as file:
        cargo_toml = pytoml.load(file)
        for key in cargo_toml["dependencies"].keys():
            print key

if __name__ == '__main__':
    sys.exit(main())
