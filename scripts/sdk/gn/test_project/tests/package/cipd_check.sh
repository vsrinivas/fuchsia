#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eu -o pipefail # Error checking

# Used to evaluate the cipd string
# shellcheck disable=SC2034
outdir="$1"
yaml="$2"

data_dir="$(grep root "${yaml}" |  cut -d\  -f2)"

far_file=""
eval far_file="${data_dir}/package.far"

if [[ ! -e "${far_file}" ]]; then
  echo "Cannot read $yaml to find far file: $far_file"
  exit 1
fi
exit 0