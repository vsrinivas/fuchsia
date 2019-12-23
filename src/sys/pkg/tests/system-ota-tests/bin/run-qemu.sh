#!/bin/bash
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Helper script for launching QEMU in a way that this can be used for local OTA
# testing, until `fx emu` is modified to support OTA.

set -e

OVMF_DIR=""
FUCHSIA_IMG=""

while (($#)); do
  case "$1" in
    --ovmf-dir)
      OVMF_DIR="$2"
      shift
      ;;
    -i|--image)
      FUCHSIA_IMG="$2"
      shift
      ;;
    *)
      echo "Unrecognized option: $1"
      exit 1
      ;;
  esac
  shift
done

if [[ -z "${OVMF_DIR}" ]]; then
  echo "--ovmf-dir must be specified"
  exit 1
fi

if [[ -z "${FUCHSIA_IMG}" ]]; then
  echo "--image must be specified"
  exit 1
fi

# Make sure the OVMF directory contains the expected EFI images.
OVMF_CODE="${OVMF_DIR}/OVMF_CODE.fd"
OVMF_VARS="${OVMF_DIR}/OVMF_VARS.fd"

if [[ ! -f "${OVMF_CODE}" ]]; then
  echo "OVMF_CODE.fd not found in ${OVMF_DIR}"
  exit 1
fi

if [[ ! -f "${OVMF_VARS}" ]]; then
  echo "OVMF_VARS.fd not found in ${OVMF_DIR}"
  exit 1
fi

# Configure the networking.
if [[ "$(uname -s)" == "Darwin" ]]; then
  IFNAME="tap0"
else
  IFNAME="qemu"
fi

MAC=""
HASH=$(echo $IFNAME | shasum)
SUFFIX=$(for i in {0..2}; do echo -n :${HASH:$(( 2 * $i )):2}; done)
MAC="52:54:00$SUFFIX"

FUCHSIA_DIR=$(fx exec bash -c 'echo $FUCHSIA_DIR')
source "${FUCHSIA_DIR}/tools/devshell/lib/prebuilt.sh"
QEMU_DIR="${PREBUILT_QEMU_DIR}/bin"

set -u

log() {
  printf "$(date '+%Y-%m-%d %H:%M:%S') [run-qemu] $@\n"
}

TMPDIR="$(mktemp -d)"
if [[ ! -d "${TMPDIR}" ]]; then
  echo >&2 "Failed to create temporary directory"
  exit 1
fi

cleanup() {
  log "cleaning up"

  # kill child processes. While there isn't technically a background process, we
  # want to make sure we kill a long-running qemu if this shell dies.
  local child_pids=$(jobs -p)
  if [[ -n "${child_pids}" ]]; then
    log "killing child processes: ${child_pids}"
    # Note: child_pids must be expanded to args here.
    kill ${child_pids} 2> /dev/null
    wait 2> /dev/null
  fi

  rm -r "${TMPDIR}"
}
trap cleanup EXIT

# Construction of a qcow image prevents qemu from writing back to the
# build-produced image file, which could cause timestamp issues with that file.
FUCHSIA_IMG_QCOW="${TMPDIR}/fuchsia.qcow2"
"${QEMU_DIR}/qemu-img" create \
  -f qcow2 \
  -b "${FUCHSIA_IMG}" \
  "${FUCHSIA_IMG_QCOW}"

# Copy in the OMVF_VARS.fd into our temp directory, which allows EFI data to be
# persisted across reboots.
OVMF_VARS_TMP="${TMPDIR}/OVMF_VARS.fd"
cp "${OVMF_VARS}" $OVMF_VARS_TMP

"${QEMU_DIR}/qemu-system-x86_64" \
  -enable-kvm \
  -m 1024 \
  -nographic \
  -drive "if=pflash,format=raw,readonly,file=${OVMF_CODE}" \
  -drive "if=pflash,format=raw,file=${OVMF_VARS_TMP}" \
  -drive "file=${FUCHSIA_IMG_QCOW},format=qcow2,if=none,id=mydisk" \
  -device virtio-blk-pci,drive=mydisk \
  -netdev "type=tap,ifname=${IFNAME},script=no,downscript=no,id=net0" \
  -device "virtio-net-pci,netdev=net0,mac=${MAC}" \
  -smp 4,threads=2 \
  -machine q35 \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  -cpu host

exit $?
