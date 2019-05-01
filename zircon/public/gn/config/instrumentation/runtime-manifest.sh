#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

INPUT="$1"
shift
OUTPUT="$1"
shift
manifest_dir="${INPUT%/*}"

SCRIPT=
for regexp in "$@"; do
  SCRIPT+="/$regexp/s@=@=$manifest_dir/@p
"
done

sed -n "$SCRIPT" "$INPUT" > "$OUTPUT"
