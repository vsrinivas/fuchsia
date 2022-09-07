#! /bin/sh
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Runs 'jq' to filter the compilation database for entries corresponding to
# files with a name fragment matching a specific string.

# Parameters:
#
# $1: stamp file path
# $2: jq binary path
# $3: compile commands input file path
# $4: compile commands output file path
# $5: the file name fragment to filter for, e.g. "api_stub.cc".

_stamp_file="${1}"
_jq="${2}"
_input_file="${3}"
_output_file="${4}"
_file_name_fragment=${5}

${_jq} \
  "map(select(.file | contains(\"${_file_name_fragment}\")))" \
  "${_input_file}" > "${_output_file}" && \
  touch "${_stamp_file}"
