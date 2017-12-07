#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -e

GUEST_SCRIPTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
FUCHSIA_DIR="${GUEST_SCRIPTS_DIR}/../../../.."
cd "${FUCHSIA_DIR}"

usage() {
  echo "usage: ${0} [options] {arm64, x86}"
  echo ""
  echo "    -g zircon.gpt   Zircon GPT disk image"
  echo "    -l image        Linux kernel image"
  echo "    -i initrd       Linux initrd"
  echo "    -r rootfs.ext2  Linux EXT2 root file-system image"
  echo ""
  exit 1
}

while getopts "g:l:i:r:" opt; do
  case "${opt}" in
  g) ZIRCON_GPT="${OPTARG}";;
  l) IMAGE="${OPTARG}";;
  i) INITRD="${OPTARG}";;
  r) ROOTFS="${OPTARG}";;
  *) usage ;;
  esac
done
shift $((OPTIND-1))

case "${1}" in
arm64)
  cd out/debug-aarch64;
  PLATFORM="hikey960";
  GUEST_BOOTDATA="\
    guest-mdi.bin
    guest-platform-id.bin";
  # NOTE(abdulla): board.mdi has paths that are relative to FUCHSIA_DIR.
  sed 's#include "#include "../../#' \
    ../../garnet/lib/machina/arch/arm64/mdi/board.mdi > board.mdi;
  ../build-zircon/tools/mdigen \
    -o guest-mdi.bin \
    board.mdi;
  ../build-zircon/tools/mkbootfs \
    -o guest-platform-id.bin \
    --vid 1 \
    --pid 1 \
    --board qemu-virt;
  IMAGE=${IMAGE:-/tmp/linux/arch/arm64/boot/Image};;
x86)
  cd out/debug-x86-64;
  PLATFORM="x86";
  IMAGE=${IMAGE:-/tmp/linux/arch/x86/boot/bzImage};;
*)
  usage;;
esac

declare -r ZIRCON=${ZIRCON:-../build-zircon/build-$PLATFORM/zircon.bin}
declare -r ZIRCON_GPT=${ZIRCON_GPT:zircon.gpt}
declare -r INITRD=${INITRD:-/tmp/toybox/initrd.gz}
declare -r ROOTFS=${ROOTFS:-/tmp/toybox/rootfs.ext2}

[ -f "${ZIRCON_GPT}" ] && GUEST_MANIFEST+=$'\n'"data/zircon.gpt=${ZIRCON_GPT}"
[ -f "${IMAGE}" ] && GUEST_MANIFEST+=$'\n'"data/image=${IMAGE}"
[ -f "${INITRD}" ] && GUEST_MANIFEST+=$'\n'"data/initrd=${INITRD}"
[ -f "${ROOTFS}" ] && GUEST_MANIFEST+=$'\n'"data/rootfs.ext2=${ROOTFS}"

grep -v 'config/devmgr' boot.manifest > guest-boot.manifest
../build-zircon/tools/mkbootfs \
  --target=boot \
  -o guest-bootdata.bin \
  guest-boot.manifest \
  $GUEST_BOOTDATA

echo "\
  data/zircon.bin=${ZIRCON}
  data/bootdata.bin=guest-bootdata.bin
  ${GUEST_MANIFEST}" > guest.manifest
../build-zircon/tools/mkbootfs \
  --target=system \
  -o host-bootdata.bin \
  guest.manifest \
  bootdata-$PLATFORM.bin \
  system_bootfs.bin
