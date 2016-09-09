#!/bin/bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

readonly SCRIPT_ROOT="$(cd $(dirname ${BASH_SOURCE[0]} ) && pwd)"
readonly FUCHSIA_URL_BASE="https://storage.googleapis.com/fuchsia-build/fuchsia"

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

# download_tarball <name> <base url> <untar directory>
function download_tarball() {
  local name="${1}"
  local base_url="${2}"
  local untar_dir="${3}"
  local base_path="${SCRIPT_ROOT}/${untar_dir}"
  local tar_path="${base_path}.tar.bz2"

  download_file_if_needed "${name}" "${FUCHSIA_URL_BASE}/${base_url}" "${base_path}" ".tar.bz2"
  if [[ -f "${tar_path}" ]]; then
    mkdir -p -- "${untar_dir}"
    (cd -- "${untar_dir}" "${tar_path}" && tar xf "${tar_path}")
    rm -f -- "${tar_path}"
  fi
}

# download_font <name>
function download_font() {
  local name="${1}"
  download_tarball "${name}" "fonts/${name}" "third_party/${name}"
}

download_font roboto
download_font robotocondensed
download_font robotomono
