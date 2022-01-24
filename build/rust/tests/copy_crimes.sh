#!/bin/bash

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eu

source="$1"
dest="$2"
depfile="$3"

echo "$dest:" > "$depfile"
cp "$source" "$dest"
