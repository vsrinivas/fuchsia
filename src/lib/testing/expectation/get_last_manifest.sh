#!/usr/bin/env bash
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

JQ="$1"
SOURCE="$2"
OUTPUT="$3"

"$JQ" '[.[-1]]' "$SOURCE" > "$OUTPUT"
