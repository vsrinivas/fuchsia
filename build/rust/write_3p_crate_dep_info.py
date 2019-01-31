#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys

# Creates the directory containing the given file.
def create_base_directory(file):
    path = os.path.dirname(file)
    try:
        os.makedirs(path)
    except os.error:
        # Already existed.
        pass

def main():
    parser = argparse.ArgumentParser(
            "Writes an info.json file for a third-party target")
    parser.add_argument("--package-name",
                        help="Package name of the crate being depended upon",
                        required=True)
    parser.add_argument("--output",
                        help="Path to output json file",
                        required=True)
    args = parser.parse_args()

    create_base_directory(args.output)
    with open(args.output, "w") as file:
        file.write(json.dumps({
            "package_name": args.package_name,
            "third_party": True,
        }, sort_keys=True, indent=4, separators=(",", ": ")))

if __name__ == '__main__':
    sys.exit(main())
