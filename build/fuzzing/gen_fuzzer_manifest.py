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
      "--bin",
      help="Path to the binary; absolute or relative to package's bin directory",
      required=True)
  parser.add_argument("--cmx", help="Optional starting manifest")
  parser.add_argument(
      "--test",
      action="store_true",
      help="Generate manifest for the fuzzer test package.")
  args = parser.parse_args()

  cmx = defaultdict(dict)
  if args.cmx:
    with open(args.cmx, "r") as f:
      cmx = json.load(f)

  if args.test:
    cmx["program"]["binary"] = "test/" + args.bin
  else:
    cmx["program"]["binary"] = "bin/" + args.bin
    if "services" not in cmx["sandbox"]:
      cmx["sandbox"]["services"] = []
    cmx["sandbox"]["services"].append("fuchsia.process.Launcher")

    if "features" not in cmx["sandbox"]:
      cmx["sandbox"]["features"] = []
    if "isolated-persistent-storage" not in cmx["sandbox"]["features"]:
      cmx["sandbox"]["features"].append("isolated-persistent-storage")

  with open(args.out, "w") as f:
    f.write(json.dumps(cmx, sort_keys=True, indent=4))


if __name__ == "__main__":
  sys.exit(main())
