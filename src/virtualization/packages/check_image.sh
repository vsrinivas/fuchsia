#!/usr/bin/env bash

# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

usage() {
  echo "usage: ${0} {source_image} {destination_image} {depfile}"
  exit 1
}

if [ "$#" -ne 3 ]; then
  usage
fi

declare -r SOURCE_IMAGE=${1}
declare -r TARGET_IMAGE=${2}
declare -r DEPFILE=${3}

# Ensure that the source image exists even if it is a dummy.
mkdir -p $(dirname ${SOURCE_IMAGE})
if [ ! -f "${SOURCE_IMAGE}" ]; then
  echo "WARNING: ${SOURCE_IMAGE} not found, using a dummy image. See" \
       "garnet/bin/guest/README.md for manual build instructions."
  touch "${SOURCE_IMAGE}"
fi

# Copy source to target.
rm -f "${TARGET_IMAGE}"
cp "${SOURCE_IMAGE}" "${TARGET_IMAGE}"

# Write a depfile to force the copy to happen when the image file changes.
echo "${TARGET_IMAGE}: ${SOURCE_IMAGE}" > $DEPFILE
