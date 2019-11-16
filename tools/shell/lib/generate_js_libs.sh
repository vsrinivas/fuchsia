#!/bin/bash

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Usage: generate_js_libs.sh <manifest_file> <list of JS files>

# This script writes a manifest file that places each of the JS files
# in /data/lib in the package's directory.

OUT_FILE="$1"
shift
rm -rf "${OUT_FILE}"
while (( $# > 0 )); do
  file=$1
  shift
  echo  "data/lib/$(basename ${file})=${file}" >> "${OUT_FILE}"
done