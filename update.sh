#!/usr/bin/env bash
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
    (cd -- "${untar_dir}" "${tar_path}" && rm -rf -- "${name}" && tar xf "${tar_path}")
    rm -f -- "${tar_path}"
  fi
}

function build_magenta_tools() {
  if [[ "${has_arguments}" = "false" ]]; then
      echo "Building magenta tools..."
      make -C "${SCRIPT_ROOT}/../magenta" tools >/dev/null
      has_arguments="true"
  fi
}

# copy_magenta_tool <name>
function copy_magenta_tool() {
  local name="${1}"
  local tool_path="${SCRIPT_ROOT}/${HOST_PLATFORM}/${name}"
  local stamp_path="${tool_path}.stamp"
  local tool_bin="${SCRIPT_ROOT}/../magenta/build-magenta-pc-x86-64/tools/${name}"
  local tool_hash="$(cd ${SCRIPT_ROOT}/../magenta && git rev-parse HEAD)"
  if [[ ! -f "${tool_path}" || ! -f "${stamp_path}" || "${tool_hash}" != "$(cat ${stamp_path})" ]]; then
      build_magenta_tools
      echo "Copying ${name}..."
      rm -f "${tool_path}"
      cp "${tool_bin}" "${tool_path}" && echo "${tool_hash}" > "${stamp_path}"
  fi
}

function download_ninja() {
  download_tool ninja "${FUCHSIA_URL_BASE}/ninja/${HOST_PLATFORM}"
}

function download_gn() {
  # TODO(abarth): gn doesn't follow the normal pattern because we download our
  # copy from Chromium's Google Storage bucket.
  download_tool gn "https://storage.googleapis.com/chromium-gn"
}

function download_cmake() {
  # TODO(jamesr): the cmake and sdk tarballs are inconsistent about how they name
  # the uploaded artifact and what directories they expect to be untarred from.
  # There's no good reason for these to be different - unify them.
  download_tarball cmake "cmake/${HOST_PLATFORM}" "${SCRIPT_ROOT}"
}

function download_toolchain() {
  download_tarball toolchain "toolchain/${HOST_PLATFORM}" "${SCRIPT_ROOT}/toolchain"
}

function download_go() {
  download_tarball go "go/${HOST_PLATFORM}" "${SCRIPT_ROOT}/${HOST_PLATFORM}"
}

function download_bootserver() {
  copy_magenta_tool bootserver
}

function download_loglistener() {
  copy_magenta_tool loglistener
}

function download_mkbootfs() {
  copy_magenta_tool mkbootfs
}

function download_all() {
  download_ninja
  download_gn
  download_cmake
  download_toolchain
  download_go
  download_bootserver
  download_loglistener
  download_mkbootfs
}

function echo_error() {
  echo "$@" 1>&2;
}

declare has_arguments="false"
declare built_magenta_tools="false"

for i in "$@"; do
case ${i} in
  --ninja)
    download_ninja
    has_arguments="true"
    shift
    ;;
  --gn)
    download_gn
    has_arguments="true"
    shift
    ;;
  --cmake)
    download_cmake
    has_arguments="true"
    shift
    ;;
  --toolchain)
    download_toolchain
    has_arguments="true"
    shift
    ;;
  --go)
    download_go
    has_arguments="true"
    shift
    ;;
  --bootserver)
    download_bootserver
    has_arguments="true"
    shift
    ;;
  --loglistener)
    download_loglistener
    has_arguments="true"
    shift
    ;;
  --mkbootfs)
    download_mkbootfs
    has_arguments="true"
    shift
    ;;
  *)
    echo_error "Unknown argument."
    exit -1
    ;;
esac
done

if [[ "${has_arguments}" = "false" ]]; then
  download_all
fi
