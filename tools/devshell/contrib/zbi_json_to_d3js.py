#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import math
import os
import sys


def main():
    parser = argparse.ArgumentParser(
        description=
        'Convert zbi tool JSON output to d3js-friendly JSON input format')
    parser.add_argument(
        '--input',
        required=True,
        type=argparse.FileType('r'),
        help='Output of `zbi --output-json=...`')
    parser.add_argument(
        '--output',
        required=True,
        help='Output JSON in d3js-friendly format',
        type=argparse.FileType('w'))
    parser.add_argument(
        '--page-size',
        type=int,
        default=4096,
        help='Round file sizes up to multiples of this size')
    args = parser.parse_args()

    zbi = json.load(args.input)
    root = {"name": "bootfs", "children": []}

    # Find bootfs
    bootfs = next((item for item in zbi if item["type"] == "BOOTFS"), None)
    if not bootfs:
        print("Could not find bootfs in ZBI")
        return 1

    # Accumulate contents and their uncompressed size
    for file in bootfs["contents"]:
        name = file["name"]
        length = file["length"]
        # Round up to page size
        length = math.ceil(length / args.page_size) * args.page_size

        # Find base directory for file
        basedir = root
        path_parts = name.split("/")
        for path_part in path_parts[:-1]:
            nextdir = next(
                (
                    child for child in basedir["children"]
                    if child["name"] == path_part), None)
            if not nextdir:
                nextdir = {"name": path_part, "children": []}
                basedir["children"].append(nextdir)
            basedir = nextdir

        # Add file
        basedir["children"].append({"name": path_parts[-1], "value": length})

    json.dump(root, args.output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
