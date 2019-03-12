#!/usr/bin/env bash

# Copyright 2019 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

declare -r DEBIAN_GUEST_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
declare -r FUCHSIA_DIR="${DEBIAN_GUEST_DIR}/../../../../.."
declare -r CIPD="${FUCHSIA_DIR}/buildtools/cipd"

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

# Clean the existing images directory.
declare -r IMAGE_DIR="${DEBIAN_GUEST_DIR}/images/${ARCH}/"
rm -rf "${IMAGE_DIR}"
mkdir -p "${IMAGE_DIR}"

# Clean the tests source directory.
declare -r TESTS_DIR="/tmp/linux-tests"
rm -rf "${TESTS_DIR}"

# Build Debian.
${DEBIAN_GUEST_DIR}/build-image.sh \
    -o "${DEBIAN_GUEST_DIR}/images/${ARCH}" \
    ${ARCH}

# Build tests.
${DEBIAN_GUEST_DIR}/mktests.sh \
    -d "${TESTS_DIR}" \
    -o "${DEBIAN_GUEST_DIR}/images/${ARCH}/tests.img" \
    -u \
    ${ARCH}
TESTS_GIT_HASH="$( cd ${TESTS_DIR} && git rev-parse --verify HEAD )"

declare -r CIPD_PATH="fuchsia_internal/linux/debian_guest-${ARCH}"
${CIPD} create \
    -in "${DEBIAN_GUEST_DIR}/images/${ARCH}" \
    -name "${CIPD_PATH}" \
    -install-mode copy \
    -tag "tests_git_revision:${TESTS_GIT_HASH}"
