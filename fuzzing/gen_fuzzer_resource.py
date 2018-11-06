#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import sys

def main():
  parser = argparse.ArgumentParser("Creates a resource file for a fuzzer")
  parser.add_argument("--out", help="Path to the output file", required=True)
  args, extra = parser.parse_known_args()

  with open(args.out, "w") as f:
    for item in extra:
      f.write(item)
      f.write("\n")

if __name__ == "__main__":
  sys.exit(main())
