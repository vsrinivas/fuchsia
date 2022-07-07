#!/usr/bin/env python3.8
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--zbi', help='Path to the zbi tool')
    parser.add_argument('--zbi-image', help='Path to the zbi image.')
    parser.add_argument('--output', help='Path to write the bootfs out.')
    args = parser.parse_args()

    # Run the zbi tool.
    command = [
        args.zbi, '--extract-raw', '--output', args.output, args.zbi_image,
        '--', '*.bootfs.bin'
    ]

    subprocess.check_call(command)


if __name__ == '__main__':
    main()
