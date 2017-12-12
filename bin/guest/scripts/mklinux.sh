#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT
#
# Clone and build a Linux kernel for use as a guest.

set -e

usage() {
  echo "usage: ${0} [options] {arm64, x86}"
  echo
  echo "  -d [defconfig]  Defconfig to use"
  echo "  -l [linux-dir]  Linux source dir"
  echo
  exit 1
}

while getopts "c:d:" OPT; do
  case $OPT in
  d) DEFCONFIG="${OPTARG}";;
  l) LINUX_DIR="${OPTARG}";;
  *) usage;;
  esac
done
shift $((OPTIND - 1))

case "${1}" in
arm64)
  declare -x ARCH=arm64;
  declare -x CROSS_COMPILE=aarch64-linux-gnu-;;
x86)
  declare -x ARCH=x86;
  declare -x CROSS_COMPILE=x86_64-linux-gnu-;;
*)
  usage;;
esac

declare -r LINUX_DIR=${LINUX_DIR:-/tmp/linux}
declare -r DEFCONFIG=${DEFCONFIG:-machina_defconfig}
echo "Building Linux with ${DEFCONFIG} in ${LINUX_DIR}"

# Shallow clone the repository.
if [ ! -d "${LINUX_DIR}" ]; then
  git clone --depth 1 https://zircon-guest.googlesource.com/third_party/linux "${LINUX_DIR}"
fi

# Update the repository.
cd "${LINUX_DIR}"
git pull

# Build Linux.
make "${DEFCONFIG}"
make -j $(getconf _NPROCESSORS_ONLN)
