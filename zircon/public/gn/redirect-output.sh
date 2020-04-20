#!/bin/sh
#
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

OUTPUT="$1"
shift

rm -f "$OUTPUT"
exec "$@" > "$OUTPUT"
