#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

usage() {
  echo "usage: ${0} [options] {arm64, x86}"
  echo
  echo "  -d [defconfig]  Defconfig to use"
  echo "  -l [linux-dir]  Linux source dir"
  echo "  -o [image]      Output location for the built kernel"
  echo
  exit 1
}

while getopts "d:l:o:" OPT; do
  case $OPT in
  d) DEFCONFIG="${OPTARG}";;
  l) LINUX_DIR="${OPTARG}";;
  o) LINUX_OUT="${OPTARG}";;
  *) usage;;
  esac
done
shift $((OPTIND - 1))

case "${1}" in
arm64)
  type aarch64-linux-gnu-gcc ||
    { echo "Required package gcc-aarch64-linux-gnu is not installed."
      echo "(sudo apt install gcc-aarch64-linux-gnu)"; exit 1; };
  declare -x ARCH=arm64;
  declare -x CROSS_COMPILE=aarch64-linux-gnu-
  declare -r LINUX_IMAGE="${LINUX_DIR}/arch/arm64/boot/Image";;
x86)
  declare -x ARCH=x86;
  declare -x CROSS_COMPILE=x86_64-linux-gnu-
  declare -r LINUX_IMAGE="${LINUX_DIR}/arch/x86/boot/bzImage";;
*)
  usage;;
esac

declare -r LINUX_DIR=${LINUX_DIR:-/tmp/linux}
declare -r DEFCONFIG=${DEFCONFIG:-machina_defconfig}
echo "Building Linux with ${DEFCONFIG} in ${LINUX_DIR}"

# Shallow clone the repository.
if [ ! -d "${LINUX_DIR}/.git" ]; then
  git clone --depth 1 https://zircon-guest.googlesource.com/third_party/linux "${LINUX_DIR}"
fi

# Update the repository.
cd "${LINUX_DIR}"
git pull
# Build Linux.
make "${DEFCONFIG}"
make -j $(getconf _NPROCESSORS_ONLN)

if [ -n "${LINUX_OUT}" ]; then
  mv "${LINUX_IMAGE}" "${LINUX_OUT}"
fi
