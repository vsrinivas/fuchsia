#!/usr/bin/env bash

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ZIRCON_ROOT="$(dirname "${BASH_SOURCE[0]}")/../.."
GN="$ZIRCON_ROOT/scripts/gn"

DRIVER_LABEL="$1"
DENYLIST_FILE="$2"
STAMP="$3"

# It's all good if and only if the file is empty.
# Every line in the file is a denylisted shared library dependency.
check_denylist() {
  local label status=0
  while read label; do
    status=1
    echo >&2 \
"*** Driver $DRIVER_LABEL is not allowed to depend on shared library $label"
    (set -x; "$GN" path . "$DRIVER_LABEL" "$label")
  done
  return $status
}

check_denylist < "$DENYLIST_FILE" && > "$STAMP"
