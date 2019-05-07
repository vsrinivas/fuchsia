#!/usr/bin/env bash

# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eo pipefail

usage() {
  echo "usage: ${0} [options] {arm64, x64}"
  echo
  echo "  -d [dir]        Source directory (default: /tmp/linux-tests)."
  echo "  -o [image]      Output location for the built image."
  echo "  -u              Update before building."
  exit 1
}

declare UPDATE="${UPDATE:-false}"
declare -r DEBIAN_GUEST_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
declare -r FUCHSIA_DIR=$(git rev-parse --show-toplevel)

while getopts "d:o:u" OPT; do
  case $OPT in
  d) TESTS_DIR="${OPTARG}";;
  o) IMAGE_OUT="${OPTARG}";;
  u) UPDATE="true";;
  *) usage;;
  esac
done
shift $((OPTIND - 1))

case "${1}" in
arm64)
  declare -r ARCH=arm64;;
x64)
  declare -r ARCH=x64;;
*)
  usage;;
esac

declare -r TESTS_DIR=${TESTS_DIR:-/tmp/linux-tests}

if [ ! -d "${TESTS_DIR}" ]; then
  git clone --depth 1 https://zircon-guest.googlesource.com/linux-tests "${TESTS_DIR}"
elif [[ "${UPDATE}" = "true" ]]; then
  pushd "${TESTS_DIR}"
  git pull --depth 1
  popd
fi

pushd "${TESTS_DIR}"
./build.sh -f ${FUCHSIA_DIR} "${ARCH}"
popd

if [ -n "${IMAGE_OUT}" ]; then
  mkdir -p $(dirname "${IMAGE_OUT}")
  cp "${TESTS_DIR}/out/linux-tests-${ARCH}.img" "${IMAGE_OUT}"
fi
