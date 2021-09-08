#!/usr/bin/env bash

# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
readonly SCRIPT_DIR
FUCHSIA_DIR=$(git rev-parse --show-toplevel)
readonly FUCHSIA_DIR
declare -r CIPD="${FUCHSIA_DIR}/.jiri_root/bin/cipd"
declare -r LINUX_BRANCH="machina-4.18"

# Ensure a valid architecture was specified.
case "${1}" in
arm64)
  ARCH=${1};;
x64)
  ARCH=${1};;
*)
  echo "usage: ${0} {arm64, x64}"
  exit 1;;
esac

# Ensure the user has authenticated with the CIPD server.
if [[ "$(${CIPD} acl-check fuchsia_internal -writer)" == *"doesn't"* ]]; then
  ${CIPD} auth-login
fi

# Create a temporary directory.
WORKING_DIR="$(mktemp -d)"
trap 'rm -rf -- "$WORKING_DIR"' EXIT

# Create the output directory.
OUTPUT_DIR="${WORKING_DIR}/output"
mkdir -p "${OUTPUT_DIR}"

# Build the kernel.
"${SCRIPT_DIR}/make_linux_kernel.sh" \
    -b "${LINUX_BRANCH}" \
    -d "machina_defconfig" \
    -l "${WORKING_DIR}/linux" \
    -o "${OUTPUT_DIR}/vm_kernel" \
    "${ARCH}"
LINUX_GIT_HASH="$( cd "${WORKING_DIR}/linux" && git rev-parse --verify HEAD )"

# Copy to the prebuilt directory.
cp -f "${OUTPUT_DIR}/vm_kernel" "${FUCHSIA_DIR}/prebuilt/virtualization/packages/linux_guest/images/${ARCH}/Image"

# Upload to CIPD.
declare -r CIPD_PATH="fuchsia_internal/linux/linux_kernel-${LINUX_BRANCH}-${ARCH}"
${CIPD} create \
    -in "${OUTPUT_DIR}" \
    -name "${CIPD_PATH}" \
    -install-mode copy \
    -tag "git_revision:${LINUX_GIT_HASH}" \
    -tag "branch:${LINUX_BRANCH}" \

# Fetch the instance ID of the just-created CIPD package. If more than one
# matches our tags, use the most recent one.
INSTANCE_ID=$(${CIPD} search \
    "${CIPD_PATH}" \
    -tag "git_revision:${LINUX_GIT_HASH}" \
    -tag "branch:${LINUX_BRANCH}" \
    | grep -v 'Instances:' \
    | cut -d ':' -f 2 \
    | head -1)

echo "Kernel git revision: ${LINUX_GIT_HASH}"
echo "Instance ID: ${INSTANCE_ID}"
