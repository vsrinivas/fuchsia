#!/usr/bin/env bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

readonly SCRIPT_ROOT="$(cd $(dirname ${BASH_SOURCE[0]} ) && pwd)"
readonly FUCHSIA_URL_BASE="https://storage.googleapis.com/fuchsia-build"

# download <url> <path>
function download() {
  local url="${1}"
  local path="${2}"
  curl -f --progress-bar -continue-at=- --location --output "${path}" "${url}"
}

# download_file_if_needed <name> <url> <base path> <extension>
function download_file_if_needed() {
  local name="${1}"
  local url="${2}"
  local base_path="${3}"
  local extension="${4}"

  local path="${base_path}${extension}"
  local stamp_path="${base_path}.stamp"
  local requested_hash="$(cat "${base_path}.sha1")"

  if [[ ! -f "${stamp_path}" ]] || [[ "${requested_hash}" != "$(cat "${stamp_path}")" ]]; then
    echo "Downloading ${name}..."
    rm -f -- "${path}"
    download "${url}/${requested_hash}" "${path}"
    echo "${requested_hash}" > "${stamp_path}"
  fi
}

# download_zipfile <name> <base url> <unzip directory>
function download_zipfile() {
  local name="${1}"
  local base_url="${2}"
  local unzip_dir="${3}"
  local base_path="${SCRIPT_ROOT}/${name}"
  local zip_path="${base_path}.zip"

  download_file_if_needed "${name}" "${FUCHSIA_URL_BASE}/${base_url}" "${base_path}" ".zip"
  if [[ -f "${zip_path}" ]]; then
    mkdir -p -- "${unzip_dir}"
    (cd -- "${unzip_dir}" && rm -rf -- "${name}" && unzip -q "${zip_path}")
    rm -f -- "${zip_path}"
  fi
}

download_zipfile rt2870 "firmware/ralink" "${SCRIPT_ROOT}/firmware"
