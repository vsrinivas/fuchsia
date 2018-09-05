#!/usr/bin/env bash

# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

usage() {
  echo "usage: ${0} {source_image} {destination_image}"
  exit 1
}

if [ "$#" -ne 2 ]; then
  usage
fi

declare -r SOURCE_IMAGE=${1}
declare -r TARGET_IMAGE=${2}

if [ -f "${SOURCE_IMAGE}" ]; then
  cp --remove-destination "${SOURCE_IMAGE}" "${TARGET_IMAGE}"
else
  echo "WARNING: ${SOURCE_IMAGE} not found, using a dummy image. See" \
       "garnet/bin/guest/README.md for manual build instructions."
  touch "${TARGET_IMAGE}"
fi
