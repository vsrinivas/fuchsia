#!/bin/bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

readonly SCRIPT_ROOT="$(cd $(dirname ${BASH_SOURCE[0]} ) && pwd)"
readonly FUCHSIA_URL_BASE="https://storage.googleapis.com/fuchsia-build/fuchsia"

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

# download_tool <name> <base url>
function download_tool() {
  local name="${1}"
  local base_url="${2}"
  local tool_path="${SCRIPT_ROOT}/${HOST_PLATFORM}/${name}"
  download_file_if_needed "${name}" "${base_url}" "${tool_path}"
  chmod a+x "${tool_path}"
}

download_tool ninja "${FUCHSIA_URL_BASE}/ninja/${HOST_PLATFORM}"

# TODO(abarth): gn doesn't follow the normal pattern because we download our
# copy from Chromium's Google Storage bucket.
download_tool gn "https://storage.googleapis.com/chromium-gn"

# download_tarball <name> <base url> <untar directory>
function download_tarball() {
  local name="${1}"
  local base_url="${2}"
  local untar_dir="${3}"
  local base_path="${SCRIPT_ROOT}/${HOST_PLATFORM}/${name}"
  local tar_path="${base_path}.tar.bz2"

  download_file_if_needed "${name}" "${FUCHSIA_URL_BASE}/${base_url}" "${base_path}" ".tar.bz2"
  if [[ -f "${tar_path}" ]]; then
    mkdir -p -- "${untar_dir}"
    (cd -- "${untar_dir}" "${tar_path}" && tar xf "${tar_path}")
    rm -f -- "${tar_path}"
  fi
}

# TODO(jamesr): the cmake and sdk tarballs are inconsistent about how they name
# the uploaded artifact and what directories they expect to be untarred from.
# There's no good reason for these to be different - unify them.
download_tarball cmake "cmake/${HOST_PLATFORM}" "${SCRIPT_ROOT}"
download_tarball toolchain "toolchain/${HOST_PLATFORM}" "${SCRIPT_ROOT}/toolchain"
download_tarball go "go/${HOST_PLATFORM}" "${SCRIPT_ROOT}/${HOST_PLATFORM}"

function download_sysroot() {
  local arch="${1}"
  local base_path="${SCRIPT_ROOT}/sysroot/${arch}-fuchsia"
  local base_url="sysroot/${arch}"
  local tar_path="${base_path}.tar.bz2"

  download_file_if_needed "${arch} sysroot" "${FUCHSIA_URL_BASE}/${base_url}" "${base_path}" ".tar.bz2"
  if [[ -f "${tar_path}" ]]; then
    (cd -- "${SCRIPT_ROOT}" && tar xf ${tar_path} -C "${SCRIPT_ROOT}/sysroot")
    rm -f -- "${tar_path}"
  fi
}

download_sysroot "aarch64"
download_sysroot "x86_64"

# build_magenta_tool <name>
function build_magenta_tool() {
  local name="${1}"
  local tool_path="${SCRIPT_ROOT}/${HOST_PLATFORM}/${name}"
  local stamp_path="${tool_path}.stamp"
  local tool_source="${SCRIPT_ROOT}/../magenta/system/tools/${name}.c"
  local tool_hash="$(cd ${SCRIPT_ROOT}/../magenta && git rev-parse HEAD)"
  if [[ ! -f "${tool_path}" || ! -f "${stamp_path}" || "${tool_hash}" != "$(cat ${stamp_path})" ]]; then
      echo "Building ${name}..."
      rm -f "${tool_path}"
      gcc "${tool_source}" -I "${SCRIPT_ROOT}/../magenta/global/include" -o "${tool_path}" && echo "${tool_hash}" > "${stamp_path}"
  fi
}

build_magenta_tool bootserver
build_magenta_tool loglistener
build_magenta_tool mkbootfs
