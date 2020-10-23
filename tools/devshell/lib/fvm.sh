#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Creates an extended raw FVM image from a source FVM.
#
# Arguments:
#   - Input FVM. The format can be raw or compressed.
#   - output image file
#   - (optional) desired minimum disk-size of the output FVM image, defaults to
#     twice the uncompressed size of the given image. If the requested size is
#     already smaller than the disk-size specified in the metadata, the
#     disk-size will remain the same. It is guaranteed that the file size of the
#     output image is the same as the disk-size in the metadata.
#
# Output:
#   (stderr) error logs on failure.
#
# Returns:
#   0 on success, 1 on failure.
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
    "${HOST_OUT_DIR}/fvm" "${fvmimg}" extend --length "${newsize}" --length-is-lowerbound
  else
    fx-error "Could not extend FVM, unable to stat FVM image ${fvm_in}"
    return 1
  fi
  return 0
}

# Finds a source FVM to generate a raw FVM image from.
#
# The raw FVM is primarily used by the emulator, and is coverted from other FVM
# formats on-demand. This locate the best source FVM to create the raw FVM from.
#
# The resulting path will generally be passed to fx-fvm-extend-image, but it's
# useful to separate the functions since failing to find an FVM isn't usually
# an error whereas failing to extend one is.
#
# Arguments:
#   None
#
# Output:
#   (stdout) path to a source FVM image if one was found, nothing otherwise.
function fx-fvm-find-raw-source {
  # Look for source FVM formats in this order. Every build that uses an FVM
  # should produce at least one of these.
  source_fvms=(
    "${IMAGE_FVM_RAW}"
    "${IMAGE_FVM_SPARSE}"
    "${IMAGE_FVM_FASTBOOT}"
  )

  for source_fvm in "${source_fvms[@]}"; do
    if [[ -n "${source_fvm}" && -f "${FUCHSIA_BUILD_DIR}/${source_fvm}" ]]; then
      echo "${FUCHSIA_BUILD_DIR}/${source_fvm}"
      return
    fi
  done
}
