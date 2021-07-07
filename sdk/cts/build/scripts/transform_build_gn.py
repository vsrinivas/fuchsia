#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import shutil
import sys


def transform_build_gn(src, dest):
    """
    Naively transforms a BUILD.gn file to work in the CTS archive.
    """
    with open(src, "r") as f:
        lines = f.readlines()

    output = []
    count = 0
    for line in lines:
        # Naively match curly braces and exclude them from the output.
        if "cts_copy_to_sdk" in line or "sdk_molecule" in line or count:
            count = _find_brace(line, count)
        else:
            output.append(line)

    with open(dest, "w") as f:
        f.writelines(output)


def _find_brace(line, count):
    for ch in line:
        if ch == '{':
            count += 1
        elif ch == '}':
            count -= 1

    return count


def main():
    parser = argparse.ArgumentParser(
        "Naively transform BUILD.gn files to work in the CTS archive.")
    parser.add_argument(
        "--source", required=True, help="Source path to BUILD.gn to update.")
    parser.add_argument(
        "--dest", required=True, help="Destination path of updated BUILD.gn.")
    args = parser.parse_args()

    if not os.path.isfile(args.source):
        print("Source file: %s does not exist" % args.source)
        return 1

    if not os.path.isdir(args.dest.rsplit("/", 1)[0]):
        print("Destination dir: %s does not exist" % args.dest)
        return 1

    ext = os.path.splitext(args.source)[1]
    if ext == ".gn":
        transform_build_gn(args.source, args.dest)
    else:
        shutil.copy(args.source, args.dest)

    return 0


if __name__ == '__main__':
    sys.exit(main())
