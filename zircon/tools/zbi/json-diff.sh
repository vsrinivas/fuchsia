#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

JQ="$1"
OUTPUT="$2"
EXPECTED="$3"
ACTUAL="$4"

set -e

rm -f "$OUTPUT"

diff -u --label=expected <("$JQ" -S . "$EXPECTED") \
        --label=actual <("$JQ" -S . "$ACTUAL")

echo OK > "$OUTPUT"

exit 0
