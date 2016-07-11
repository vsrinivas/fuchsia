#!/bin/bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

readonly SCRIPT_ROOT="$(cd $(dirname ${BASH_SOURCE[0]} ) && pwd)"

case "$(uname -s)" in
  Darwin)
    readonly HOST_PLATFORM="mac"
    ;;
  Linux)
    readonly HOST_PLATFORM="linux64"
    ;;
  *)
    echo "Unknown operating system. Cannot install build tools."
    exit 1
    ;;
esac

readonly GN_PATH="${SCRIPT_ROOT}/${HOST_PLATFORM}/gn"
readonly GN_STAMP_PATH="${GN_PATH}.stamp"
readonly GN_HASH="$(cat "${GN_PATH}.sha1")"
readonly GN_BUCKET=chromium-gn
readonly GN_URL="https://storage.googleapis.com/${GN_BUCKET}/${GN_HASH}"

if [[ ! -f "${GN_STAMP_PATH}" ]] || [[ "${GN_HASH}" != "$(cat "${GN_STAMP_PATH}")" ]]; then
  rm -f -- "${GN_PATH}"
  echo "Downloading gn..."
  curl --progress-bar -continue-at=- --location --output "${GN_PATH}" "${GN_URL}"
  chmod a+x "${GN_PATH}"
  echo "${GN_HASH}" > "${GN_STAMP_PATH}"
fi
