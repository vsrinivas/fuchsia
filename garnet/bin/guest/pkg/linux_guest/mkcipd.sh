#!/usr/bin/env bash

# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

declare -r LINUX_GUEST_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
declare -r FUCHSIA_DIR="${LINUX_GUEST_DIR}/../../../../.."
declare -r CIPD="${FUCHSIA_DIR}/buildtools/cipd"
declare -r SOURCE_DIR="/tmp/linux_guest"
declare -r LINUX_VERSION="4.18"

case "${1}" in
arm64)
  ARCH=${1};;
x64)
  ARCH=${1};;
*)
  echo "usage: ${0} {arm64, x64}"
  exit 1;;
esac

${CIPD} auth-login

rm -rf "${SOURCE_DIR}"
rm -rf "${LINUX_GUEST_DIR}/images/${ARCH}/Image"
rm -rf "${LINUX_GUEST_DIR}/images/${ARCH}/tests.img"
rm -rf "${LINUX_GUEST_DIR}/images/${ARCH}/disk.img"

${LINUX_GUEST_DIR}/mklinux.sh \
    -b "machina-${LINUX_VERSION}" \
    -d "machina_defconfig" \
    -l "${SOURCE_DIR}/linux" \
    -o "${LINUX_GUEST_DIR}/images/${ARCH}/Image" \
    ${ARCH}
LINUX_GIT_HASH="$( cd ${SOURCE_DIR}/linux && git rev-parse --verify HEAD )"

${LINUX_GUEST_DIR}/mktests.sh \
    -d "${SOURCE_DIR}/linux-tests" \
    -o "${LINUX_GUEST_DIR}/images/${ARCH}/tests.img" \
    -u \
    ${ARCH}
TESTS_GIT_HASH="$( cd ${SOURCE_DIR}/linux-tests && git rev-parse --verify HEAD )"

${LINUX_GUEST_DIR}/mksysroot.sh \
    -d "${SOURCE_DIR}/toybox-${ARCH}" \
    -s "${SOURCE_DIR}/dash" \
    -o "${LINUX_GUEST_DIR}/images/${ARCH}/disk.img" \
    -u \
    ${ARCH}
TOYBOX_GIT_HASH="$( cd ${SOURCE_DIR}/toybox-${ARCH} && git rev-parse --verify HEAD )"
DASH_GIT_HASH="$( cd ${SOURCE_DIR}/dash && git rev-parse --verify HEAD )"

declare -r CIPD_PATH="fuchsia_internal/linux/linux_guest-${LINUX_VERSION}-${ARCH}"

${CIPD} create \
    -in "${LINUX_GUEST_DIR}/images/${ARCH}" \
    -name "${CIPD_PATH}" \
    -install-mode copy \
    -tag "kernel_git_revision:${LINUX_GIT_HASH}" \
    -tag "tests_git_revision:${TESTS_GIT_HASH}" \
    -tag "toybox_git_revision:${TOYBOX_GIT_HASH}" \
    -tag "dash_git_revision:${DASH_GIT_HASH}"

INSTANCE_ID=$(${CIPD} describe \
    "${CIPD_PATH}" \
    -version "kernel_git_revision:${LINUX_GIT_HASH}" \
    -version "tests_git_revision:${TESTS_GIT_HASH}" \
    -version "toybox_git_revision:${TOYBOX_GIT_HASH}" \
    -version "dash_git_revision:${DASH_GIT_HASH}" \
    | grep -oP "Instance ID: *\K\w+(?=$)" )

echo "Kernel git revision: ${LINUX GIT HASH}"
echo "Tests git revision: ${TESTS GIT HASH}"
echo "Toybox git revision: ${TOYBOX GIT HASH}"
echo "Dash git revision: ${DASH GIT HASH}"
echo "Instance ID: ${INSTANCE_ID}"
