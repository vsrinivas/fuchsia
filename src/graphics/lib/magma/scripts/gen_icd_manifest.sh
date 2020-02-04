#!/bin/bash

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eo pipefail

usage() {
  echo "Usage: $(basename $0) path_field [output_file]"
  echo "Example: $(basename $0) /usr/lib/libvulkan_magma.so"
  echo "Formats a standard Vulkan ICD Manifest string, writing the result to output_file if"
  echo "  specified, or stdout otherwise. Can be used in the build as a gn action or manually on"
  echo "  the development host machine."
}

# The loader queries the ICD version from vkEnumerateInstanceVersion only if the api_version
# reported in the manifest is at least 1.1.0.

manifest_contents="{
  \"file_format_version\": \"1.0.0\",
  \"ICD\": {
    \"library_path\": \"${1}\",
    \"api_version\": \"1.1.0\"
  }
}"

if [[ $# == 1 ]]; then
  echo "${manifest_contents}"
elif [[ $# == 2 ]]; then
  echo "${manifest_contents}" > ${2}
else
  usage
  exit -1
fi
