#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Script to extract line-separated text files with contents from create-system's images.json.
"""

import argparse
import json
import os
import sys


def main():
    parser = argparse.ArgumentParser(
        description=
        "Parse assembly create-system output manifest to get package info.")
    parser.add_argument(
        "--package-set",
        type=str,
        required=True,
        choices=["base", "cache"],
        help="The package set for which to emit metadata.")
    parser.add_argument("--contents", type=str, choices=["name", "manifest"])
    parser.add_argument(
        "--images-manifest",
        type=argparse.FileType('r'),
        required=True,
        help="Path to images.json created by `ffx assembly create-system`.")
    parser.add_argument(
        "--output",
        type=argparse.FileType('w'),
        required=True,
        help="Path to which to write desired output list.")
    args = parser.parse_args()

    images_manifest = json.load(args.images_manifest)

    lines = []
    for image in images_manifest:
        contents = image.get("contents", dict())
        if "packages" in contents:
            for package in contents["packages"][args.package_set]:
                lines.append(package[args.contents])

    for l in lines:
        args.output.write(l)
        args.output.write("\n")

    return 0


if __name__ == '__main__':
    sys.exit(main())
