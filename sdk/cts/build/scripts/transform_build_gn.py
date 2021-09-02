#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import re
import shutil
import sys

_targets_to_remove = [
    "cts_copy_to_sdk",
    "cts_source_library",
    "sdk_molecule",
    "cts_artifacts",
    "group(\"prebuilts\")",
    "action(\"update_test_manifest\")",
]


def transform_build_gn(src, dest, in_tree_mappings, version):
    """
    Naively transforms a BUILD.gn file to work in the CTS archive.
    """
    with open(src, "r") as f:
        lines = f.readlines()

    output = []
    count = 0
    for line in lines:
        # Naively match curly braces and exclude them from the output.
        if any(target in line for target in _targets_to_remove) or count:
            count = _find_brace(line, count)
        # Replace dependency with CTS version.
        elif any(key in line for key in in_tree_mappings.keys()):
            for key in in_tree_mappings.keys():
                if key in line:
                    output.append(line.replace(key, in_tree_mappings[key]))
        elif version != "" and "package_name" in line:
            # Append between the quotes of `package_name = "name"`
            output.append(re.sub(r'\"(.+)\"', r'"\1_%s"' % version, line))
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
    parser.add_argument(
        "--cts_version", required=False, help="CTS version name.")
    args = parser.parse_args()

    if not os.path.isfile(args.source):
        print("Source file: %s does not exist" % args.source)
        return 1

    if not os.path.isdir(args.dest.rsplit("/", 1)[0]):
        print("Destination dir: %s does not exist" % args.dest)
        return 1

    version = ""
    if args.cts_version:
        version = args.cts_version
    in_tree_mappings = {
        "//zircon/system/ulib/fbl": f"//prebuilt/cts/{version}/cts/pkg/fbl",
        "//zircon/system/ulib/zxtest": f"//prebuilt/cts/{version}/cts/pkg/zxtest",
    }

    ext = os.path.splitext(args.source)[1]
    if ext == ".gn":
        transform_build_gn(args.source, args.dest, in_tree_mappings, version)
    else:
        shutil.copy(args.source, args.dest)

    return 0


if __name__ == '__main__':
    sys.exit(main())
