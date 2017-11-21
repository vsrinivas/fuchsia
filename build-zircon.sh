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
  printf '%s: [-c] [-v] [-V] [-A] [-H] [-p projects] [-t target] [-o outdir]\n' "$0"
  printf 'Note: Passing extra arguments to make is not supported.\n'
}

make_zircon_common() {
  make --no-print-directory -C "${ROOT_DIR}/zircon" \
    -j ${JOBS} DEBUG_BUILDROOT=../../zircon "$@"
}

make_zircon_target() {
  make_zircon_common \
    BUILDROOT=${ZIRCON_BUILDROOT} TOOLS=${OUTDIR}/build-zircon/tools "$@"
}

declare ASAN="${ASAN:-false}"
declare CLEAN="${CLEAN:-false}"
declare HOST_ASAN="${HOST_ASAN:-false}"
declare OUTDIR="${OUTDIR:-${ROOT_DIR}/out}"
declare VERBOSE="${VERBOSE:-0}"

while getopts "AcHht:p:o:vV" opt; do
  case "${opt}" in
    A) ASAN="true" ;;
    c) CLEAN="true" ;;
    H) HOST_ASAN="true" ;;
    h) usage ; exit 0 ;;
    o) OUTDIR="${OPTARG}" ;;
    t) TARGET="${OPTARG}" ;;
    p) PROJECTS="${OPTARG}" ;;
    v) VERBOSE="1" ;;
    V) VERBOSE="2" ;;
    *) usage 1>&2 ; exit 1 ;;
  esac
done

if [[ "${PROJECTS}" = "" ]]; then
    if [[ "${TARGET}" != "" ]]; then
        PROJECTS="${TARGET}"
    else
        PROJECTS="zircon-pc-x86-64" # Default
    fi
fi

declare -a ARCHLIST
if [[ "${PROJECTS}" == *arm64* ]]; then
    ARCHLIST+=(arm64)
fi
if [[ "${PROJECTS}" == *x86-64* ]]; then
    ARCHLIST+=(x86-64)
fi
readonly -a ARCHLIST
if [[ "${#ARCHLIST[@]}" == 0 ]]; then
    echo >&2 "Cannot figure out architectures from $PROJECTS"
    exit 2
fi

readonly ASAN CLEAN HOST_ASAN PROJECTS OUTDIR VERBOSE
readonly ZIRCON_BUILDROOT="${OUTDIR}/build-zircon"

if [[ "${CLEAN}" = "true" ]]; then
  rm -rf -- "${ZIRCON_BUILDROOT}"
fi

# These variables are picked up by make from the environment.
case "${VERBOSE}" in
  1) QUIET=0 ; V=0 ;;
  2) QUIET=0 ; V=1 ;;
  *) QUIET=1 ; V=0 ;;
esac
export QUIET V

if [[ "${ASAN}" = "true" ]]; then
  readonly ASAN_ZIRCON=true
  readonly ASAN_ULIB=false
else
  readonly ASAN_ZIRCON=false
  readonly ASAN_ULIB=true
fi

# Build host tools.
make_zircon_common \
  BUILDDIR=${OUTDIR}/build-zircon HOST_USE_ASAN="${HOST_ASAN}" tools

for ARCH in "${ARCHLIST[@]}"; do
    # Build primary userland and sysroot.
    make_zircon_target PROJECT="user-${ARCH}" \
        BUILDDIR_SUFFIX= USE_ASAN="${ASAN_ZIRCON}" user-only
    # Build alternate shared libraries (ASan).
    make_zircon_target PROJECT="user-${ARCH}" \
        BUILDDIR_SUFFIX=-ulib USE_ASAN="${ASAN_ULIB}" \
        ENABLE_ULIB_ONLY=true ENABLE_BUILD_SYSROOT=false
done

# Build kernels.
IFS=','
for project in $PROJECTS; do
    make_zircon_target PROJECT="$project" kernel-only
done
