#!/bin/bash

# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Invokes a formatter as expected by golden_files() (i.e., via stdio).
# If the supplied file does not exist, the given output path is `touch`ed.

depfile="$1"
input="$2"
output="$3"
shift 3

mkdir -p $(dirname "$output")
if [[ -f "$input" ]]; then
    "$@" < "$input" > "$output"
else
    touch "$output"
fi

mkdir -p $(dirname "$depfile")
echo "$output: $input" > "$depfile"
