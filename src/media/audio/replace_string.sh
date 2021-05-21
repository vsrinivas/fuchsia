#!/bin/bash
#
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

readonly INPUT_FILE="$1"
readonly OUTPUT_FILE="$2"
readonly OLD_PATTERN="$3"
readonly NEW_PATTERN="$4"

set -e
rm -f "$OUTPUT_FILE"
sed -e "s#${OLD_PATTERN}#${NEW_PATTERN}#" "$INPUT_FILE" > "$OUTPUT_FILE"
