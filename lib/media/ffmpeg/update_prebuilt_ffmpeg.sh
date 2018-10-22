#!/usr/bin/env bash
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This updates the prebuilt ffmpeg artifacts in prebuilt/{x64,arm64} to the version specified in
# prebuilt/version. See README.md for instructions on updating the prebuilts.

set -eu

readonly SCRIPT_ROOT="$(cd $(dirname ${BASH_SOURCE[0]} ) && pwd)"
readonly PREBUILT_VERSION="$(cat ${SCRIPT_ROOT}/prebuilt/version)"

readonly X64_DIR="${SCRIPT_ROOT}/prebuilt/x64"
readonly ARM64_DIR="${SCRIPT_ROOT}/prebuilt/arm64"
readonly URL_BASE="https://storage.googleapis.com/fuchsia/lib/ffmpeg"

mkdir -p "${X64_DIR}"
mkdir -p "${ARM64_DIR}"

# download <url> <path>
function download() {
  local url="${1}"
  local path="${2}"
  curl -f --progress-bar -continue-at=- --location --output "${path}" "${url}"
}

# download_file_if_needed <name> <version> <base url> <base path>
function download_file_if_needed() {
  local name="${1}"
  local version="${2}"
  local base_url="${3}"
  local base_path="${4}"

  local path="${base_path}/${name}"
  local stamp_path="${base_path}.stamp"
  local requested_url="${base_url}/${version}/${name}"

  if [[ ! -f "${stamp_path}" ]] || [[ "${requested_url}" != "$(cat "${stamp_path}")" ]]; then
    echo "Downloading ${requested_url} to ${path}"
    rm -f -- "${path}"
    download "${requested_url}" "${path}"
    echo "${requested_url}" > "${stamp_path}"
  fi
}

# download_tarball <name> <base url> <untar directory>
function download_tarball() {
  local name="${1}"
  local base_url="${2}"
  local untar_dir="${3}"
  local filename="libffmpeg.tar.gz"
  local tar_path="${untar_dir}/${filename}"

  download_file_if_needed "${filename}" "${PREBUILT_VERSION}" "${base_url}" "${untar_dir}"
  if [[ -f "${tar_path}" ]]; then
    mkdir -p -- "${untar_dir}"
    (cd -- "${untar_dir}" && tar xzf "${tar_path}" 2>/dev/null)
    rm -f -- "${tar_path}"
  fi
}

download_tarball "prebuilt/libffmpeg_x64.tar.gz" "${URL_BASE}/fuchsia-amd64" "${X64_DIR}"
download_tarball "prebuilt/libffmpeg_arm64.tar.gz" "${URL_BASE}/fuchsia-arm64" "${ARM64_DIR}"
