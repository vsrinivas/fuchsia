#!/bin/sh
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# add_tag_to_manifest.sh tag input-file output-file
#
# Add a tag (#tag) to each dest entry in the given manfiest file, writing to
# the given output path.

set -e

readonly tag="$1"
readonly input="$2"
readonly output="$3"

while read line
do
  if [ -n "${line}" ]; then
    left="${line%%=*}"
    right="${line#*=}"
    printf "%s#%s=%s\n" "$left" "$tag" "$right"
  fi
done < "${input}" > "${output}"
