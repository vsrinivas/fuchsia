#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Arguments:
#   - Input fvm. The format can be raw or compressed.
#   - output image file
#   - (optional) desired minimum disk-size of the output fvm image, defaults to twice the
#     uncompressed size of the given image. If the requested size is already smaller than the
#     disk-size specified in the metadata, the disk-size will remain the same. It is guaranteed
#     that the file size of the output image is the same as the disk-size in the metadata.
function fx-fvm-extend-image {
  fvm_in=$1
  fvmimg=$2

  # Store the decompressed file with a deterministic path to facilitate testing
  "${HOST_OUT_DIR}/fvm" "${fvm_in}.decompressed" decompress --default "${fvm_in}"
  # Rename the decompressed file to |fvmimg| and perform extension.
  mv "${fvm_in}.decompressed" "${fvmimg}"

  stat_flags=()
  if [[ $(uname) == "Darwin" ]]; then
    stat_flags+=("-x")
  fi
  stat_output=$(stat "${stat_flags[@]}" "${fvmimg}")
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
    echo -n "Creating disk image..."
    "${HOST_OUT_DIR}/fvm" "${fvmimg}" extend --length "${newsize}" --length-is-lowerbound
    echo "done"
  else
    fx-error "Could not extend fvm, unable to stat fvm image"
    return 1
  fi
  return 0
}
