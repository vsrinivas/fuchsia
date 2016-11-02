#!/bin/bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eu

readonly SCRIPT_ROOT="$(cd $(dirname ${BASH_SOURCE[0]} ) && pwd)"

# download <url> <path>
function download() {
  local url="${1}"
  local path="${2}"
  curl -f --progress-bar -continue-at=- --location --output "${path}" "${url}"
}

# download_file_if_needed <name> <base path> <extension>
function download_file_if_needed() {
  local name="${1}"
  local base_path="${2}"
  local extension="${3}"

  local path="${base_path}.${extension}"
  local stamp_path="${base_path}.stamp"
  local requested_url="$(cat "${base_path}.version")"

  if [[ ! -f "${stamp_path}" ]] || [[ "${requested_url}" != "$(cat "${stamp_path}")" ]]; then
    echo "Downloading ${name}..."
    rm -f -- "${path}"
    download "${requested_url}" "${path}"
    echo "${requested_url}" > "${stamp_path}"
  fi
}

# download_tarball <name> <base url> <untar directory> <extension>
function download_tarball() {
  local name="${1}"
  local base_url="${2}"
  local untar_dir="${3}"
  local extension="${4}"
  local base_path="${SCRIPT_ROOT}/${untar_dir}"
  local tar_path="${base_path}.${extension}"

  download_file_if_needed "${name}" "${base_path}" "${extension}"
  case "x${extension}" in
    xtar*) local extract_cmd="tar xf" ;;
    xzip) local extract_cmd="unzip" ;;
    *) echo "unknown archive type" && exit -1 ;;
  esac
  if [[ -f "${tar_path}" ]]; then
    mkdir -p -- "${untar_dir}"
    (cd -- "${untar_dir}" "${tar_path}" && ${extract_cmd} "${tar_path}")
    rm -f -- "${tar_path}"
  fi
}

# download_font <name> <extract>
function download_font() {
  local name="${1}"
  local extension="${2}"
  download_tarball "${name}" "fonts/${name}" "third_party/${name}" "${extension}"
}

download_font material zip
download_font roboto tar.bz2
download_font robotocondensed tar.bz2
download_font robotomono tar.bz2
