#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly ROOT_DIR="$(dirname "${SCRIPT_DIR}")"

readonly HOST_ARCH=$(uname -m)
readonly HOST_OS=$(uname | tr '[:upper:]' '[:lower:]')
readonly HOST_TRIPLE="${HOST_ARCH}-${HOST_OS}"


JOBS=`getconf _NPROCESSORS_ONLN` || {
  Cannot get number of processors
  exit 1
}

set -eo pipefail; [[ "${TRACE}" ]] && set -x

usage() {
  printf >&2 '%s: [-c] [-v] [-t target] [-o outdir]\n' "$0" && exit 1
}

build() {
  local target="$1" outdir="$2" clean="$3" verbose="$4"
  local magenta_buildroot="${outdir}/build-magenta"

  if [[ "${clean}" = "true" ]]; then
    rm -rf -- "${magenta_buildroot}"
  fi

  case "${target}" in
    "x86_64") local magenta_target="magenta-pc-x86-64" ;;
    "aarch64") local magenta_target="magenta-qemu-arm64" ;;
    "rpi3") local magenta_target="magenta-rpi3-arm64" ;;
    "*") echo "unknown target '${target}'" 1>&2 && exit 1;;
  esac

  pushd "${ROOT_DIR}/magenta" > /dev/null
  if [[ "${verbose}" = "true" ]]; then
      export QUIET=0
  else
      export QUIET=1
  fi
  # build magenta (including its portion of the sysroot) for the target architecture
  make -j ${JOBS} ${magenta_build_type_flags:-} BUILDROOT=${magenta_buildroot} ${magenta_target} BUILDDIR_SUFFIX=
  # build host tools
  make -j ${JOBS} BUILDDIR=${outdir}/build-magenta tools
  popd > /dev/null
}

declare CLEAN="${CLEAN:-false}"
declare TARGET="${TARGET:-x86_64}"
declare OUTDIR="${OUTDIR:-${ROOT_DIR}/out}"
declare VERBOSE="${VERBOSE:-false}"

while getopts "cd:t:o:v" opt; do
  case "${opt}" in
    c) CLEAN="true" ;;
    o) OUTDIR="${OPTARG}" ;;
    t) TARGET="${OPTARG}" ;;
    v) VERBOSE="true" ;;
    *) usage;;
  esac
done

readonly CLEAN TARGET OUTDIR VERBOSE

build "${TARGET}" "${OUTDIR}" "${CLEAN}" "${VERBOSE}"
