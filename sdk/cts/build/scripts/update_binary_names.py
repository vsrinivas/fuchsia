#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import re
import shutil
import sys


def append_to_binary_name(src, dest, suffix):
    """
    Appends to the "binary" key in a .CMX file.
    """
    with open(src, "r") as f:
        cmx = json.load(f)

    cmx["program"]["binary"] += suffix

    with open(dest, "w") as f:
        json.dump(cmx, f, indent=4)


def append_in_quotes(src, dest, sub, suffix):
    """
    Finds the line containing sub and appends inside double quotes.
    """
    lines = []
    with open(src, "r") as f:
        for line in f:
            # TODO(johnshamoon): This will break if sub equals a variable.
            if sub in line:
                # Capture everything between quotes in group 1, then append _SHA
                # to group 1.
                line = re.sub(r'\"(.+)\"', r'"\1%s"' % suffix, line)
            lines.append(line)

    with open(dest, "w") as f:
        f.writelines(lines)


def main():
    parser = argparse.ArgumentParser(
        "Update values to duplicate names in the CTS archive.")
    parser.add_argument(
        "--source", required=True, help="Source path to file to update.")
    parser.add_argument(
        "--dest", required=True, help="Destination path of updated file.")
    parser.add_argument(
        "--suffix", required=True, help="Suffix to append to binary names.")
    args = parser.parse_args()

    if not os.path.isfile(args.source):
        print("Source file: %s does not exist" % args.source)
        return 1

    if not os.path.isdir(args.dest.rsplit("/", 1)[0]):
        print("Destination dir: %s does not exist" % args.dest)
        return 1

    if not args.suffix:
        print("Suffix cannot be empty.")
        return 1

    ext = os.path.splitext(args.source)[1]
    if ext == ".gn":
        append_in_quotes(args.source, args.dest, "output_name", args.suffix)
    elif ext == ".cmx":
        append_to_binary_name(args.source, args.dest, args.suffix)
    elif ext == ".cml":
        # CML files are JSON5 and require another library to load as json, so we
        # can't treat them the same as CMX files.
        append_in_quotes(args.source, args.dest, "binary", args.suffix)
    else:
        shutil.copy(args.source, args.dest)

    return 0


if __name__ == '__main__':
    sys.exit(main())
