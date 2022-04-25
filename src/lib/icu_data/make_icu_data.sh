#! /bin/sh
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script recovers the file icudtl.dat from the extracted contents of
# a known far archive.

# The binary used for file extraction.
_far="${1}"

# The web engine archive to extract the file from.
_web_engine_archive="${2}"

## The file name containing mapping of the file name to hash of the unpacked
## archive file.
_contents_file="${3}"

# The output directory to save the resulting file.
_output_dir="${4}"

_data_line=$(cat "${_contents_file}" | grep "icudtl.dat=")
_data_file="${_data_line#icudtl.dat=}"

"${_far}" extract-file \
  --archive="${_web_engine_archive}" \
  --file="${_data_file}" \
  --output="${_output_dir}/icudtl.dat"
