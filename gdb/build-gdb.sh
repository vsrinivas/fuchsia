#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. >/dev/null 2>&1 && pwd)"

# N.B. This must be an absolute path.
readonly ROOT_DIR="$(dirname "${SCRIPT_DIR}")"

readonly HOST_ARCH=$(uname -m)
readonly HOST_OS=$(uname | tr '[:upper:]' '[:lower:]')
readonly HOST_TRIPLE="${HOST_ARCH}-${HOST_OS}"

if [[ "x${HOST_OS}" == "xlinux" ]]; then
  readonly DEFAULT_JOBS=$(grep ^processor /proc/cpuinfo | wc -l)
elif [[ "x${HOST_OS}" == "xdarwin" ]]; then
  readonly DEFAULT_JOBS=$(sysctl -n hw.ncpu)
else
  echo "Unsupported system: ${HOST_OS}" 1>&2
  exit 1
fi

[[ "${TRACE}" ]] && set -x

usage() {
  printf >&2 '%s: [-c] [-o outdir] [-d destdir] [-j jobs]\n' "$0"
  echo >&2 "-c:         clean the build directories first"
  echo >&2 "-o outdir:  build the tools here"
  echo >&2 "-d destdir: install the tools here"
  echo >&2 "-j jobs:    passed to make"
  exit 1
}

build() {
  local outdir="$1" destdir="$2" clean="$3" jobs="$4"
  # This is where gdb will be installed.
  # We don't pass it to --prefix however so as to not encode
  # any path info in the install. Instead we pass --prefix=/
  # and use DESTDIR=${prefix} during the make install.
  local prefix="${destdir}/${HOST_TRIPLE}/gdb"
  local builddir="${outdir}/build-gdb-${HOST_TRIPLE}"

  if [[ "${clean}" = "true" ]]; then
    rm -rf -- "${builddir}"
  fi

  rm -rf -- "${prefix}"

  mkdir -p -- "${builddir}"
  pushd "${builddir}"
  # TODOs:
  # Better separate debug dir?
  # Require python? (instead of only using it if found)
  # Require expat? (instead of only using it if found)
  # Augment/change auto-load directories?
  config_prefix="/"
  # The // is a hack to preserve relocatability which doesn't handle prefix=/.
  # The specified value is the default, but given that we specify a
  # system.gdbinit file we set it explicitly to document the relationship.
  # Specifying prefix=/ is already a hack, so this is coping with that hack.
  config_datadir="//share/gdb"
  [[ -f "${builddir}/Makefile" ]] || ${ROOT_DIR}/third_party/gdb/configure \
    --prefix="$config_prefix" \
    --enable-targets=arm-elf,aarch64-elf,aarch64-fuchsia,x86_64-elf,x86_64-fuchsia \
    --disable-werror \
    --disable-nls \
    --with-gdb-datadir="$config_datadir" \
    --with-system-gdbinit="$config_datadir/system-gdbinit/fuchsia.py"
  make -j "${jobs}" all-gdb
  make -j "${jobs}" install-gdb DESTDIR="${prefix}"
  popd

  local stamp="$(LC_ALL=POSIX cat $(find "${prefix}" -type f | sort) | shasum -a1  | awk '{print $1}')"
  echo "${stamp}" > "${prefix}/.stamp"
}

declare CLEAN="${CLEAN:-false}"
declare OUTDIR="${OUTDIR:-${ROOT_DIR}/out}"
declare DESTDIR="${DESTDIR:-${OUTDIR}/toolchain}"
declare JOBS="${DEFAULT_JOBS}"

while getopts "cd:j:o:" opt; do
  case "${opt}" in
    c) CLEAN="true" ;;
    d) DESTDIR="${OPTARG}" ;;
    j) JOBS="${OPTARG}" ;;
    o) OUTDIR="${OPTARG}" ;;
    *) usage;;
  esac
done

absolute_path() {
  local -r path="$1"
  case "$path" in
    /*) echo "$path" ;;
    *) echo "$(pwd)/$path" ;;
  esac
}

# These must be absolute paths.
OUTDIR=$(absolute_path "${OUTDIR}")
DESTDIR=$(absolute_path "${DESTDIR}")

readonly CLEAN OUTDIR DESTDIR JOBS

build "${OUTDIR}" "${DESTDIR}" "${CLEAN}" "${JOBS}"
