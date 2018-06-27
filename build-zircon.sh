#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly ROOT_DIR="$(dirname "${SCRIPT_DIR}")"

JOBS=`getconf _NPROCESSORS_ONLN` || {
  Cannot get number of processors
  exit 1
}

set -eo pipefail; [[ "${TRACE}" ]] && set -x

usage() {
  echo "$0 <options> <extra-make-arguments>"
  echo ""
  echo "Options:"
  echo "  -c: Clean before building"
  echo "  -v: Level 1 verbosity"
  echo "  -V: Level 2 verbosity"
  echo "  -A: Build with ASan"
  echo "  -H: Build host tools with ASan"
  echo "  -j N: Passed along to make (number of parallel jobs)"
  echo "  -t <target>: Architecture (GN style) to build, instead of all"
  echo "  -o <outdir>: Directory in which to put the build-zircon directory."
  echo ""
  echo "Additional arguments may be passed to make using standard FOO=bar syntax."
  echo "E.g., build-zircon.sh USE_CLANG=true"
}

declare ASAN="false"
declare CLEAN="false"
declare HOST_ASAN="false"
declare OUTDIR="${ROOT_DIR}/out"
declare VERBOSE="0"
declare -a ARCHLIST=(arm64 x64)

while getopts "AcHhj:t:p:o:vV" opt; do
  case "${opt}" in
    A) ASAN="true" ;;
    c) CLEAN="true" ;;
    H) HOST_ASAN="true" ;;
    h) usage ; exit 0 ;;
    j) JOBS="${OPTARG}" ;;
    o) OUTDIR="${OPTARG}" ;;
    t) ARCHLIST=("${OPTARG}") ;;
    v) VERBOSE="1" ;;
    V) VERBOSE="2" ;;
    *) usage 1>&2 ; exit 1 ;;
  esac
done
shift $(($OPTIND - 1))

readonly ASAN CLEAN HOST_ASAN PROJECTS OUTDIR VERBOSE
readonly ZIRCON_BUILDROOT="${OUTDIR}/build-zircon"
readonly -a ARCHLIST

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
  readonly NOT_ASAN=false
else
  readonly NOT_ASAN=true
fi

make_zircon_common() {
  (test $QUIET -ne 0 || set -x
   exec make --no-print-directory -C "${ROOT_DIR}/zircon" \
             -j ${JOBS} DEBUG_BUILDROOT=../../zircon "$@")
}

# Build host tools.
make_zircon_common \
  BUILDDIR=${OUTDIR}/build-zircon HOST_USE_ASAN="${HOST_ASAN}" tools "$@"

make_zircon_target() {
  make_zircon_common \
    BUILDROOT=${ZIRCON_BUILDROOT} TOOLS=${OUTDIR}/build-zircon/tools "$@"
}

for ARCH in "${ARCHLIST[@]}"; do
    # Build without ASan for sysroot.  If all of userland will be ASan,
    # then this build is only user libraries.
    make_zircon_target PROJECT="${ARCH}" \
        BUILDDIR_SUFFIX= ENABLE_ULIB_ONLY="${ASAN}" "$@"

    # Always build at least the libraries with ASan, but never the sysroot.
    make_zircon_target PROJECT="${ARCH}" \
        BUILDDIR_SUFFIX=-asan USE_ASAN=true ENABLE_BUILD_SYSROOT=false \
        ENABLE_ULIB_ONLY="${NOT_ASAN}"
done
