#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

GUEST_SCRIPTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
FUCHSIA_DIR="${GUEST_SCRIPTS_DIR}/../../../.."
cd "${FUCHSIA_DIR}"

DEFAULT_GN_PACKAGE_LIST=(
  garnet/packages/guest
)
DEFAULT_GN_PACKAGES=$(IFS=, ; echo "${DEFAULT_GN_PACKAGE_LIST[*]}")

usage() {
  echo "usage: ${0} [options] {hikey960, vim2, x86, qemu-x86, qemu-arm64}"
  echo
  echo "  -A            Use ASAN in GN"
  echo "  -g            Use Goma"
  echo "  -p [package]  Set package, defaults to ${DEFAULT_GN_PACKAGES}"
  echo
  exit 1
}

while getopts "Agp:" FLAG; do
  case "${FLAG}" in
  A) GN_ASAN="--variant=asan";;
  g) $HOME/goma/goma_ctl.py ensure_start;
     GN_GOMA="--goma";
     NINJA_GOMA="-j1024";;
  p) PACKAGE="${OPTARG}";;
  *) usage;;
  esac
done
shift $((OPTIND - 1))

case "${1}" in
hikey960) ;&
vim2)
  ARCH="aarch64";
  PLATFORM="${1}";;
x86) ;&
qemu-x86)
  ARCH="x86-64";
  PLATFORM="x86";;
qemu-arm64)
  ARCH="aarch64";
  PLATFORM="arm64";;
*)
  usage;;
esac

scripts/build-zircon.sh \
  -p $PLATFORM

build/gn/gen.py \
  --target_cpu=$ARCH \
  --platforms=$PLATFORM \
  --packages="${PACKAGE:-${DEFAULT_GN_PACKAGES}},build/packages/bootfs" \
  --args bootfs_packages=true \
  $GN_ASAN \
  $GN_GOMA

buildtools/ninja \
  -C out/debug-$ARCH \
  $NINJA_GOMA

case "${1}" in
hikey960)
  zircon/scripts/flash-hikey \
    -u \
    -n \
    -b out/build-zircon/build-$PLATFORM \
    -d out/debug-$ARCH/user.bootfs;;
vim2)
  zircon/scripts/flash-vim2 \
    -b out/build-zircon/build-$PLATFORM \
    -d out/debug-$ARCH/user.bootfs;;
x86)
  out/build-zircon/tools/bootserver \
    -1 \
    out/build-zircon/build-$PLATFORM/zircon.bin \
    out/debug-$ARCH/user.bootfs;;
qemu-x86)
  zircon/scripts/run-zircon-x86-64 \
    -k \
    -V \
    -g \
    -x out/debug-$ARCH/user.bootfs \
    -o out/build-zircon/build-$PLATFORM/;;
qemu-arm64)
  zircon/scripts/run-zircon-arm64 \
    -V \
    -x out/debug-$ARCH/user.bootfs \
    -o out/build-zircon/build-$PLATFORM \
    -m 2048 \
    -- -M virt,gic_version=3;;
esac
