#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

readonly GS_BUCKET="https://storage.googleapis.com/fuchsia-build"
readonly SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
readonly PREBUILTS_DIR="$(cd "${SCRIPT_DIR}/prebuilt" && pwd)"
readonly PREBUILTS_MANIFEST="${PREBUILTS_DIR}/manifest"

# Install prebuilts into a .gitignore'd directory to keep things clean
mkdir -p "${PREBUILTS_DIR}/downloads"
readonly INSTALL_DIR="$(cd "${PREBUILTS_DIR}/downloads" && pwd)"

# download checks the stamp file against the version file and downloads a new
# copy of the prebuilt if necessary.
function download() {
  local version_path="${1}"
  local required_version=$(cat "${version_path}")
  local prebuilt_url=${GS_BUCKET}/${version_path}/${required_version}
  local install_path="${PREBUILTS_DIR}/downloads/${version_path}"
  local stamp_path="${PREBUILTS_DIR}/downloads/${version_path}.stamp"

  if [[ ! -f "${stamp_path}" || "${required_version}" != "$(cat "${stamp_path}")" ]]; then
    mkdir -p $(dirname "${install_path}")
    rm -f "${install_path}"
    echo "Downloading ${install_path}"
    if ! curl -s -f -continue-at=- --location --output "${install_path}" "${prebuilt_url}"; then
      echo "Error downloading ${install_path}: maybe it doesn't exist in Google Storage?"
      exit 1
    fi
    chmod +x "${install_path}"
    echo "${required_version}" > "${stamp_path}"
  fi
}

cd "${PREBUILTS_DIR}/versions"
for version_path in $(cat "${PREBUILTS_MANIFEST}"); do
  download "${version_path#./}"
done
