#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
from os import write
import sys
import test_module


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--outfile", required=True)
    parser.add_argument("--message", required=True)
    args = parser.parse_args()

    if args.message != "A message":
        raise ValueError(
            msg="The test script was not passed the correct message argument")

    result = test_module.sum_two_numbers(5, 37)
    if result != 42:
        raise ValueError(msg='The result of the test_module was invalid')

    with open(args.outfile, "w") as outfile:
        outfile.write(args.message)


if __name__ == '__main__':
    main()
