#!/bin/bash

# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# Generate a raw binary file from the given input file.

readonly PREFIX="$1"
readonly INPUT="$2"
readonly OUTPUT="$3"

"${PREFIX}objcopy" -O binary "${INPUT}" "${OUTPUT}"
