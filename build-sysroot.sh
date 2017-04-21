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
  printf >&2 '%s: [-c] [-r] [-t target] [-o outdir] [-d destdir]\n' "$0" && exit 1
}

build() {
  local target="$1" outdir="$2" destdir="$3" clean="$4" release="$5"
  local sysroot="${destdir}/${target}-fuchsia"
  local magenta_buildroot="${outdir}/build-magenta"

  if [[ "${clean}" = "true" ]]; then
    rm -rf -- "${magenta_buildroot}"
  fi

  case "${target}" in
    "x86_64") local magenta_target="magenta-pc-x86-64" ;;
    "aarch64") local magenta_target="magenta-qemu-arm64" ;;
    "*") echo "unknown target '${target}'" 1>&2 && exit 1;;
  esac

  local magenta_sysroot="${magenta_buildroot}/build-${magenta_target}/sysroot"

  rm -rf -- "${sysroot}" && mkdir -p -- "${sysroot}"

  pushd "${ROOT_DIR}/magenta"
  rm -rf -- "${magenta_sysroot}"
  # build the sysroot for the target architecture
  make -j ${JOBS} ${magenta_build_type_flags:-} BUILDROOT=${magenta_buildroot} ${magenta_target} BUILDDIR_SUFFIX=
  # build host tools
  make -j ${JOBS} BUILDDIR=${outdir}/build-magenta tools
  popd

  cp -r -- \
    "${magenta_sysroot}/include" \
    "${magenta_sysroot}/lib" \
    "${sysroot}"

  if [[ -d "${magenta_sysroot}/debug-info" ]]; then
    cp -r -- \
      "${magenta_sysroot}/debug-info" \
      "${sysroot}"
  fi

  # These are magenta headers for use in building host tools outside
  # of the magenta tree.
  local magenta_host_include="${magenta_buildroot}/build-${magenta_target}/tools/include"
  local out_magenta_host_dir="${outdir}/magenta-host-${target}"
  rm -rf -- "${out_magenta_host_dir}"
  mkdir -p -- "${out_magenta_host_dir}"
  cp -r -- \
   "${magenta_host_include}" \
   "${out_magenta_host_dir}"
}

declare CLEAN="${CLEAN:-false}"
declare TARGET="${TARGET:-x86_64}"
declare OUTDIR="${OUTDIR:-${ROOT_DIR}/out}"
declare DESTDIR="${DESTDIR:-${OUTDIR}/sysroot}"
declare RELEASE="${RELEASE:-false}"

while getopts "cd:t:o:r" opt; do
  case "${opt}" in
    c) CLEAN="true" ;;
    d) DESTDIR="${OPTARG}" ;;
    o) OUTDIR="${OPTARG}" ;;
    r) RELEASE="true" ;;
    t) TARGET="${OPTARG}" ;;
    *) usage;;
  esac
done

readonly CLEAN TARGET OUTDIR DESTDIR RELEASE

build "${TARGET}" "${OUTDIR}" "${DESTDIR}" "${CLEAN}" "${RELEASE}"
