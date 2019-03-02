#!/bin/sh
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# manifest_add_dest_prefix.sh prefix input-file output-file
#
# Split manifest lines, adding the given prefix to the dest path.

set -e

readonly prefix="$1"
readonly input="$2"
readonly output="$3"

while read line
do
  if [ -n "${line}" ]; then
    left="${line%%=*}"
    right="${line#*=}"
    printf "%s%s=%s%s\n" "$prefix" "$left" "$right"
  fi
done < "${input}" > "${output}"
