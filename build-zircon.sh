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
  printf '%s: [-c] [-v] [-V] [-A] [-H] [-p project] [-t target] [-o outdir]\n' "$0"
  printf 'Note: Passing extra arguments to make is not supported.\n'
}

build() {
  local project="$1" outdir="$2" clean="$3" verbose="$4" asan="$5" host_asan="$6"
  local zircon_buildroot="${outdir}/build-zircon"

  if [[ "${clean}" = "true" ]]; then
    rm -rf -- "${zircon_buildroot}"
  fi

  local asan_zircon asan_ulib
  if [[ "${asan}" = "true" ]]; then
      asan_zircon=true
      asan_ulib=false
  else
      asan_zircon=false
      asan_ulib=true
  fi

  pushd "${ROOT_DIR}/zircon" > /dev/null
  case "${verbose}" in
    1) QUIET=0 ; V=0 ;;
    2) QUIET=0 ; V=1 ;;
    *) QUIET=1 ; V=0 ;;
  esac
  export QUIET
  # build host tools
  make -j ${JOBS} V=${V} \
    BUILDDIR=${outdir}/build-zircon DEBUG_BUILDROOT=../../zircon \
    HOST_USE_ASAN="${host_asan}" tools
  # build zircon (including its portion of the sysroot) for the target architecture
  make -j ${JOBS} V=${V} \
    ${zircon_build_type_flags:-} ${project} \
    BUILDROOT=${zircon_buildroot} DEBUG_BUILDROOT=../../zircon \
    TOOLS=${outdir}/build-zircon/tools USE_ASAN=${asan_zircon} \
    BUILDDIR_SUFFIX=
  # Build the alternate shared libraries (ASan).
  make -j ${JOBS} V=${V} \
    ${zircon_build_type_flags:-} ${project} \
    BUILDROOT=${zircon_buildroot} DEBUG_BUILDROOT=../../zircon \
    TOOLS=${outdir}/build-zircon/tools USE_ASAN=${asan_ulib} \
    ENABLE_ULIB_ONLY=true ENABLE_BUILD_SYSROOT=false BUILDDIR_SUFFIX=-ulib
  popd > /dev/null
}

declare ASAN="${ASAN:-false}"
declare CLEAN="${CLEAN:-false}"
declare HOST_ASAN="${HOST_ASAN:-false}"
declare PROJECT="${PROJECT:-zircon-pc-x86-64}"
declare OUTDIR="${OUTDIR:-${ROOT_DIR}/out}"
declare VERBOSE="${VERBOSE:-0}"

while getopts "AcHht:p:o:vV" opt; do
  case "${opt}" in
    A) ASAN="true" ;;
    c) CLEAN="true" ;;
    H) HOST_ASAN="true" ;;
    h) usage ; exit 0 ;;
    o) OUTDIR="${OPTARG}" ;;
    t) case "${OPTARG}" in
          "x86_64"|"x86-64") PROJECT="zircon-pc-x86-64" ;;
          "aarch64"|"arm64") PROJECT="zircon-qemu-arm64" ;;
          "gauss") PROJECT="zircon-gauss-arm64" ;;
          "odroidc2") PROJECT="zircon-odroidc2-arm64" ;;
          "hikey960") PROJECT="zircon-hikey960-arm64" ;;
          *) echo "unknown target '${OPTARG}'" 1>&2 && exit 1;;
        esac
        ;;
    p) PROJECT="${OPTARG}" ;;
    v) VERBOSE="1" ;;
    V) VERBOSE="2" ;;
    *) usage 1>&2 ; exit 1 ;;
  esac
done

readonly ASAN CLEAN HOST_ASAN PROJECT OUTDIR VERBOSE

build "${PROJECT}" "${OUTDIR}" "${CLEAN}" "${VERBOSE}" "${ASAN}" "${HOST_ASAN}"
