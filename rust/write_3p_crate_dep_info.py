#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys

def main():
    parser = argparse.ArgumentParser(
            "Writes an info.json file for a third-party target")
    parser.add_argument("--crate-name",
                        help="Name of the crate being depended upon",
                        required=True)
    parser.add_argument("--output",
                        help="Path to output json file",
                        required=True)
    args = parser.parse_args()

    with open(args.output, "w") as file:
        file.write(json.dumps({
            "crate_name": args.crate_name,
            "third_party": True,
        }, sort_keys=True, indent=4, separators=(",", ": ")))

if __name__ == '__main__':
    sys.exit(main())
