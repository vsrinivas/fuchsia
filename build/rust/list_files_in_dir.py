#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys

def main():
    parser = argparse.ArgumentParser(
            "Lists all of the filepaths in the provided directory")
    parser.add_argument("--dir",
                        help="Path to directory",
                        required=True)
    args = parser.parse_args()

    # Print out one line for each file in the directory
    for f in os.listdir(args.dir):
        print os.path.abspath(os.path.join(args.dir, f))

if __name__ == '__main__':
    sys.exit(main())
