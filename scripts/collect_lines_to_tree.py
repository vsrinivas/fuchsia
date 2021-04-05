#!/usr/bin/env python3.8

# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import re
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Turn a log and a regex into a JSON weighted tree.")
    parser.add_argument(
        "--infile",
        type=argparse.FileType("r"),
        required=True,
        help="Input text file with lines to be processed",
    )
    parser.add_argument(
        "--outfile",
        type=argparse.FileType("w"),
        required=True,
        help="Output JSON file")
    parser.add_argument(
        "--regex",
        required=True,
        help="Regex whose first capture group will be used to get tree paths",
    )
    parser.add_argument(
        "--root", required=True, help="Name of root node in the tree")
    args = parser.parse_args()

    # Initialize the tree at the root node.
    # Use a map for children for fast lookup.
    root = {"name": args.root, "children": {}}

    # Process input lines
    regex = re.compile(args.regex)
    for line in args.infile.readlines():
        m = regex.match(line)
        if m:
            path = os.path.normpath(m.group(1)).split(os.path.sep)
            node = root
            # Traverse the tree, possibly creating nodes along the path, until
            # arriving at the leaf
            for part in path[:-1]:
                node = node["children"].setdefault(
                    part, {
                        "name": part,
                        "children": {}
                    })
            # Create or update leaf node
            leaf = node["children"].setdefault(
                path[-1], {
                    "name": path[-1],
                    "value": 0
                })
            leaf["value"] += 1

    # Transform tree into output format.
    # Use a list for children for presentation such as with d3js.
    output = {"name": args.root, "children": []}

    def add_children(parent, children):
        add_to = parent["children"]
        for name, child in children.items():
            if "children" in child:
                new_child = {"name": name, "children": []}
                add_children(new_child, child["children"])
            else:
                new_child = child
            add_to.append(new_child)

    add_children(output, root["children"])

    # Write output
    json.dump(output, args.outfile)
    return 0


if __name__ == "__main__":
    sys.exit(main())
