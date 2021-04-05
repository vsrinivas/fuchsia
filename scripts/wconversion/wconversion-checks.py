#!/usr/bin/env python3.8

# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# After turning on -Wconversion globally, run a fuchsia build, but save
# the output.
#
#   $ fx build > /tmp/output 2>&1  # Can also use `ninja`
#   $ ./scripts/wconversion/wconversion-checks.py /tmp/output -i=-Wsign-conversion
#   2815 errors in unique locations
#   source files: 2073
#   header files: 741
#   other: 1
#   third party: 180
#   Counter({'-Wshorten-64-to-32': 1144, '-Wimplicit-int-conversion': 1038, ...})
#   No. of failed targets: 679

import os.path
import re
import sys

WERROR_PATTERN = re.compile("\[-Werror,(.+)\]")
TARGET_PATTERN = re.compile("\[(\d+)/\d+\]")

def parse_args():
  from argparse import ArgumentParser, FileType

  parser = ArgumentParser(description="Script for grabbing statistics on -Wconversion errors from an `fx build` or `ninja` output")

  parser.add_argument("infile",
                      type=FileType('r'),
                      help="File containing ninja logs")

  # Ignore the `-W` because python will interpret it as an argument, even if it
  # is wrapped in single quotes in bash.
  parser.add_argument("-i", "--ignore",
                      action="append",
                      default=[],
                      help="Set of warnings to ignore (\"-Wshorten-64-to-32\", \"-Wsign-conversion\", etc.)")

  return parser.parse_args()

def main():
  args = parse_args()
  ignored = set(args.ignore)

  failed_targets = set()
  errors = set()
  third_party = 0
  source_files = 0
  header_files = 0
  other = 0
  cur_target = None

  for line in args.infile:
    if TARGET_PATTERN.search(line):
      step, action, target = line.split(maxsplit=2)
      cur_target = target.strip()

    match = WERROR_PATTERN.search(line)
    if not match:
      continue

    err = match.group(1)
    if err in ignored:
      continue

    error_loc = line.find(": error: ")
    assert error_loc >= 0, "Could not find error line"

    path = line[:error_loc]
    path, line, col = path.split(":")
    line = int(line)
    col = int(col)

    errors.add((path, line, col, err))

    assert cur_target is not None, "Expected a target"
    if cur_target not in failed_targets:
      failed_targets.add(cur_target)


  for path, line, col, err in errors:
    if path.startswith("../../third_party/"):
      third_party += 1

    if path.endswith(".c") or path.endswith(".cc") or path.endswith(".cpp"):
      source_files += 1
    elif path.endswith(".h") or path.endswith(".hpp"):
      header_files += 1
    else:
      other += 1

  print(len(errors), "errors in unique locations")
  print("source files:", source_files)
  print("header files:", header_files)
  print("other:", other)

  print("third party:", third_party)

  # Distribution of errors
  from collections import Counter
  print(Counter((x[3] for x in errors)))

  print("No. of failed targets:", len(failed_targets))

  return 0

if __name__ == "__main__":
  sys.exit(main())
