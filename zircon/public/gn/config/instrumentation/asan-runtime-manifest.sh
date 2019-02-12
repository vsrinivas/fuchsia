#!/bin/sh
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

INPUT="$1"
OUTPUT="$2"

manifest_dir="${INPUT%/*}"

sed -n "/asan/s@=@=$manifest_dir/@p" "$INPUT" > "$OUTPUT"
