#!/usr/bin/env bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly ROOT_DIR="$(dirname "${SCRIPT_DIR}")"

readonly HOST_ARCH="$(uname -m)"
readonly HOST_OS="$(uname | tr '[:upper:]' '[:lower:]')"
readonly HOST_TRIPLE="${HOST_ARCH}-${HOST_OS}"

if [[ "x${HOST_OS}" == "xlinux" ]]; then
  readonly QEMU_HOST_FLAGS="${QEMU_FLAGS} --disable-gtk --enable-sdl=internal --enable-kvm"
elif [[ "x${HOST_OS}" == "xdarwin" ]]; then
  readonly QEMU_HOST_FLAGS="${QEMU_FLAGS} --enable-cocoa"
else
  echo "unsupported system: ${HOST_OS}" 1>&2
  exit 1
fi

set -eo pipefail; [[ "${TRACE}" ]] && set -x

usage() {
  printf >&2 '%s: [-c] [-o outdir] [-d destdir] [-j jobs] [-s srcdir]\n' "$0"
  exit 1
}

build() {
  local srcdir="$1" outdir="$2" destdir="$3" clean="$4" jobs="$5"

  if [[ "${clean}" = "true" ]]; then
    rm -rf -- "${outdir}/build-qemu-${HOST_TRIPLE}"
  fi

  rm -rf "${destdir}/qemu-${HOST_TRIPLE}"

  mkdir -p -- "${outdir}/build-qemu-${HOST_TRIPLE}"
  pushd "${outdir}/build-qemu-${HOST_TRIPLE}"
  ${srcdir}/configure \
    ${QEMU_HOST_FLAGS} \
    --prefix= \
    --target-list=aarch64-softmmu,x86_64-softmmu \
    --without-system-pixman \
    --without-system-fdt \
    --disable-vte \
    --disable-vnc \
    --disable-docs \
    --disable-curl \
    --disable-debug-info \
    --disable-qom-cast-debug \
    --disable-guest-agent \
    --disable-bluez \
    --disable-brlapi \
    --disable-gnutls \
    --disable-gcrypt \
    --disable-nettle \
    --disable-virtfs \
    --disable-vhost-net \
    --disable-vhost-scsi \
    --disable-vhost-vsock \
    --disable-libusb \
    --disable-smartcard \
    --disable-tools \
    --disable-tasn1
  make -j "${jobs}"
  make DESTDIR="${destdir}/qemu-${HOST_TRIPLE}" install
  popd
}

declare CLEAN="${CLEAN:-false}"
declare SRCDIR="${SRCDIR:-}"
declare OUTDIR="${OUTDIR:-${ROOT_DIR}/out}"
declare DESTDIR="${DESTDIR:-${OUTDIR}}"
declare JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN)}"

while getopts "cd:j:o:s:" opt; do
  case "${opt}" in
    c) CLEAN="true" ;;
    d) DESTDIR="$(cd "${OPTARG}"; pwd -P)" ;;
    j) JOBS="${OPTARG}" ;;
    o) OUTDIR="$(cd "${OPTARG}"; pwd -P)" ;;
    s) SRCDIR="$(cd "${OPTARG}"; pwd -P)" ;;
    *) usage;;
  esac
done

if [[ ! -d "${SRCDIR}" ]]; then
  # Do all our work in a temporary directory, then rm it when we're done.
  readonly TEMPDIR="$(mktemp -d ${ROOT_DIR}/qemu.XXXXXXXXXX)"
  trap "rm -rf -- ${TEMPDIR}" EXIT

  git clone --recursive "https://fuchsia.googlesource.com/third_party/qemu" "${TEMPDIR}"
  SRCDIR="${TEMPDIR}"
fi

readonly CLEAN SRCDIR OUTDIR DESTDIR JOBS

build "${SRCDIR}" "${OUTDIR}" "${DESTDIR}" "${CLEAN}" "${JOBS}"
