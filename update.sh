#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

readonly SCRIPT_ROOT="$(cd $(dirname ${BASH_SOURCE[0]} ) && pwd)"
readonly FUCHSIA_URL_BASE="https://storage.googleapis.com/fuchsia-build/fuchsia"
readonly FUCHSIA_CIPD_URL="https://storage.googleapis.com/fuchsia"

readonly HOST_ARCH=$(uname -m)
readonly HOST_OS=$(uname | tr '[:upper:]' '[:lower:]')
readonly HOST_TRIPLE="${HOST_ARCH}-${HOST_OS}"

source "${SCRIPT_ROOT}/vars.sh"
source "${SCRIPT_ROOT}/download.sh"

case "$(uname -s)" in
  Darwin)
    readonly HOST_PLATFORM="mac"
    readonly CIPD_PLATFORM="mac-amd64"
    ;;
  Linux)
    readonly HOST_PLATFORM="linux64"
    readonly CIPD_PLATFORM="linux-amd64"
    ;;
  *)
    echo "Unknown operating system. Cannot install build tools."
    exit 1
    ;;
esac

# download_tool <name> <base url>
function download_tool() {
  local name="${1}"
  local base_url="${2}"
  local tool_path="${SCRIPT_ROOT}/${HOST_PLATFORM}/${name}"
  download_executable "${name}" "${FUCHSIA_URL_BASE}/${base_url}" "${tool_path}"
}

# download_tarball <name> <base url> <untar directory>
function download_host_tarball() {
  local name="${1}"
  local base_url="${2}"
  local untar_dir="${3}"
  local base_path="${SCRIPT_ROOT}/${HOST_PLATFORM}/${name}"
  download_tarball "${name}" "${FUCHSIA_URL_BASE}/${base_url}" "${untar_dir}" "${base_path}"
}

# download_cipd_package <name> <package name> <unpackage directory>
function download_cipd_package() {
  local name="${1}"
  local package="${2}"
  local unpackage_dir="${3}"
  local base_path="${SCRIPT_ROOT}/${HOST_PLATFORM}/${name}"

  download_zip "${name}" "${FUCHSIA_CIPD_URL}/${package}/${CIPD_PLATFORM}" "${unpackage_dir}" "${base_path}"
}

function download_ninja() {
  download_tool ninja "ninja/${HOST_PLATFORM}"
}

function download_gn() {
  download_tool gn "gn/${HOST_PLATFORM}"
}

function download_clang() {
  download_cipd_package clang clang "${BUILDTOOLS_CLANG_DIR}"
}

function download_rust() {
  download_cipd_package rust rust "${BUILDTOOLS_RUST_DIR}"
}

function download_go() {
  download_cipd_package go go "${BUILDTOOLS_GO_DIR}"
}

function download_godepfile() {
  download_tool godepfile "godepfile/${HOST_PLATFORM}"
}

function download_qemu() {
  download_cipd_package qemu qemu "${BUILDTOOLS_QEMU_DIR}"
}

if [[ "${HOST_PLATFORM}" == "linux64" ]]; then
  function download_sysroot() {
    download_host_tarball sysroot "sysroot/${HOST_PLATFORM}" "${SCRIPT_ROOT}"
  }
fi

function download_gdb() {
  download_host_tarball gdb "gdb/${HOST_PLATFORM}" "${SCRIPT_ROOT}"
}

# Download the default set of tools.
# This doesn't include things like gdb which isn't needed by the bots.

function download_all_default() {
  download_ninja
  download_gn
  download_clang
  download_rust
  download_go
  download_godepfile
  download_qemu
  if [[ "${HOST_PLATFORM}" == "linux64" ]]; then
    download_sysroot
  fi
  # See IN-29. Need to distinguish bots from humans.
  download_gdb
}

function echo_error() {
  echo "$@" 1>&2;
}

declare has_arguments="false"

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
  --clang)
    download_clang
    has_arguments="true"
    shift
    ;;
  --rust)
    download_rust
    has_arguments="true"
    shift
    ;;
  --go)
    download_go
    has_arguments="true"
    shift
    ;;
  --godepfile)
    download_godepfile
    has_arguments="true"
    shift
    ;;
  --qemu)
    download_qemu
    has_arguments="true"
    shift
    ;;
  --sysroot)
    if [[ "${HOST_PLATFORM}" == "linux64" ]]; then
      download_sysroot
    fi
    has_arguments="true"
    shift
    ;;
  --gdb)
    download_gdb
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
  download_all_default
fi
