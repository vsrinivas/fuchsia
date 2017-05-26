#!/usr/bin/env bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

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

# download_executable <name> <base url> <path>
function download_executable() {
  local name="${1}"
  local base_url="${2}"
  local tool_path="${3}"
  download_file_if_needed "${name}" "${base_url}" "${tool_path}"
  chmod a+x "${tool_path}"
}

# download_tarball <name> <base url> <untar directory> <base path>
function download_tarball() {
  local name="${1}"
  local base_url="${2}"
  local untar_dir="${3}"
  local base_path="${4}"
  local tar_path="${base_path}.tar.bz2"

  download_file_if_needed "${name}" "${base_url}" "${base_path}" ".tar.bz2"
  if [[ -f "${tar_path}" ]]; then
    mkdir -p -- "${untar_dir}"
    (cd -- "${untar_dir}" && rm -rf -- "${name}" && tar xf "${tar_path}")
    rm -f -- "${tar_path}"
  fi
}
