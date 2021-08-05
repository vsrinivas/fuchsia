#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Assert that drivers aren't included in the build without a driver component"""

import json
import argparse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--metadata_file',
        type=argparse.FileType('r', encoding='UTF-8'),
        help=
        'Path to the metadata file which tried to collect data about drivers')
    parser.add_argument(
        '--output',
        type=argparse.FileType('w', encoding='UTF-8'),
        help='The path for the output file for the build.')

    args = parser.parse_args()

    lines = args.metadata_file.readlines()
    if (len(lines) != 0):
        print("All drivers should have driver components. These do not:")
        for line in lines:
            print("   " + line, end="")
        print(
            "Please visit https://fuchsia.dev/fuchsia-src/development/drivers/developer_guide/driver-development for how drivers should be included."
        )
        raise Exception("Found drivers that weren't driver components")
    args.output.write("passed")


if __name__ == "__main__":
    main()
