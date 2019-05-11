#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

INPUT="$1"
OUTPUT="$2"
shift 2

trap 'rm -f "$OUTPUT"' ERR HUP INT TERM

manifest_dir="${INPUT%/*}"

IFS='='
while read target source; do
  for arg in "$@"; do
    if [[ "$target" == $arg ]]; then
      echo "$target=$manifest_dir/$source"
      break
    fi
  done
done < "$INPUT" > "$OUTPUT"
