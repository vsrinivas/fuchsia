#!/bin/sh
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

JQ="$1"
JSON="$2"
OUTPUT="$3"
QUERY="$4"

"$JQ" "$QUERY" < "$JSON" > "$OUTPUT" || rm -f "$OUTPUT"
