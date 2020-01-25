#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Arguments:
#   - raw image file
#   - extended image file
#   - (optional) size of extended image file, defaults to original*2
function fx-fvm-extend-image {
  fvmraw=$1
  fvmimg=$2

  stat_flags=()
  if [[ $(uname) == "Darwin" ]]; then
    stat_flags+=("-x")
  fi
  stat_output=$(stat "${stat_flags[@]}" "${fvmraw}")
  if [[ "$stat_output" =~ Size:\ ([0-9]+) ]]; then
    size="${BASH_REMATCH[1]}"
    recommended_size=$((size * 2))
    if [[ $# -gt 2 && -n "$3" ]]; then
      newsize=$3
      if [[ "${newsize}" -le "${size}" ]]; then
        fx-error "Image size has to be greater than ${size} bytes.  Recommended value is ${recommended_size} bytes."
        return 1
      fi
    else
      newsize="${recommended_size}"
    fi
    # We must take a copy of the build artifact, rather than re-use it, as we
    # need to modify it in order to extend it.
    echo -n "Creating disk image..."
    cp "${fvmraw}" "${fvmimg}"
    "${ZIRCON_TOOLS_DIR}/fvm" "${fvmimg}" extend --length "${newsize}"
    echo "done"
  else
    fx-error "Could not extend fvm, unable to stat fvm image"
    return 1
  fi
  return 0
}
