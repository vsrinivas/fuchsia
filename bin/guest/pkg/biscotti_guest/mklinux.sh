#!/usr/bin/env bash

# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

BISCOTTI_GUEST_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
LINUX_GUEST_DIR="${BISCOTTI_GUEST_DIR}/../linux_guest"

usage() {
  echo "usage: ${0} [options] {arm64, x64}"
  echo
  echo "  -l [linux-dir]  Linux source dir"
  echo
  exit 1
}

MKLINUX_ARGS=""

while getopts "l:o:" OPT; do
  case $OPT in
  l) MKLINUX_ARGS="${MKLINUX_ARGS} -l $OPTARG";;
  *) usage;;
  esac
done
shift $((OPTIND - 1))

case "${1}" in
arm64|x64) LINUX_ARCH="${1}" ;;
*) usage;;
esac

$LINUX_GUEST_DIR/mklinux.sh \
    -b biscotti-4.18 \
    -d biscotti_defconfig \
    -o "${BISCOTTI_GUEST_DIR}/images/${LINUX_ARCH}/Image" \
    ${MKLINUX_ARGS} \
    ${LINUX_ARCH}
