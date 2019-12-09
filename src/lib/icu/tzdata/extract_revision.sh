#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script extracts the time zone data revision (e.g. "2019c") from the given
# path to "zoneinfo64.res" and writes it to the given output path.
#
# Usage:
#   ./extract_revision.sh <path/to/zoneinfo64.res> <path/to/output/file>

# Fail fast
set -euo pipefail

# Run `strings` on the file with 2-byte little-Endian chars.
# Search for a version string of the appropriate pattern, exit on failure.
# Examples: "2019c", "2023z"
count="0"
out=$(strings --encoding=l $1 | grep -E '^20[0-9][0-9][a-z]$') &&
  count=$(printf "%s" "$out" | wc -w)

if [ "$count" = "1" ]; then
  printf "%s" "$out" >$2
else
  # >&2 is stderr
  echo "Expected exactly one version string in '$1'. Found $count." >&2
  exit -1
fi
