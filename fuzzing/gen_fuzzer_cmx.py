#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import sys
from collections import defaultdict


def main():
  parser = argparse.ArgumentParser("Creates a component manifest for a fuzzer")
  parser.add_argument("--out", help="Path to the output file", required=True)
  parser.add_argument(
      "--bin", help="Package relative path to the binary", required=True)
  parser.add_argument("--cmx", help="Optional starting manifest")
  args = parser.parse_args()

  cmx = defaultdict(dict)
  if args.cmx:
    with open(args.cmx, "r") as f:
      cmx = json.load(f)

  cmx["program"]["binary"] = "bin/" + args.bin

  if "services" not in cmx["sandbox"]:
    cmx["sandbox"]["services"] = []
  cmx["sandbox"]["services"].append("fuchsia.process.Launcher")

  if "features" not in cmx["sandbox"]:
    cmx["sandbox"]["features"] = []
  if "persistent-storage" not in cmx["sandbox"]["features"]:
    cmx["sandbox"]["features"].append("persistent-storage")

  with open(args.out, "w") as f:
    f.write(json.dumps(cmx, sort_keys=True, indent=4))


if __name__ == "__main__":
  sys.exit(main())
