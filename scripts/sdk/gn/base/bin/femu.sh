#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Enable error checking for all commands
err_print() {
  echo "Error at $1"
  stty sane
}
trap 'err_print $0:$LINENO' ERR
set -e

SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_SRC_DIR}/fuchsia-common.sh" || exit $?
FUCHSIA_BUCKET="${DEFAULT_FUCHSIA_BUCKET}"
IMAGE_NAME="qemu-x64"

# Export variables needed here but also in femu.sh
export FUCHSIA_SDK_PATH="$(realpath "${SCRIPT_SRC_DIR}/..")"
export FUCHSIA_IMAGE_WORK_DIR="$(realpath "${SCRIPT_SRC_DIR}/../images")"

emu_help () {
  # Extract command-line argument help from emu script, similar to fx-print-command-help
  sed -n -e 's/^## //p' -e 's/^##$//p' "${SCRIPT_SRC_DIR}/devshell/emu" | grep -v "usage: fx emu"
}

usage () {
  echo "Usage: $0"
  echo "  [--work-dir <directory to store image assets>]"
  echo "    Defaults to ${FUCHSIA_IMAGE_WORK_DIR}"
  echo "  [--bucket <fuchsia gsutil bucket>]"
  echo "    Defaults to ${FUCHSIA_BUCKET}"
  echo "  [--image <image name>]"
  echo "    Defaults to ${IMAGE_NAME}"
  echo "  [--authorized-keys <file>]"
  echo "    The authorized public key file for securing the device.  Defaults to "
  echo "    the output of 'ssh-add -L'"
  echo "  [--help] [-h]"
  echo "    Show command line options for femu.sh and emu subscript"
  echo
  echo "Extra emu arguments:"
  emu_help
}
AUTH_KEYS_FILE=""
EMU_ARGS=()

# Parse command line
while (( "$#" )); do
case $1 in
    --work-dir)
    shift
    FUCHSIA_IMAGE_WORK_DIR="${1:-.}"
    ;;
    --bucket)
    shift
    FUCHSIA_BUCKET="${1}"
    ;;
    --image)
    shift
    IMAGE_NAME="${1}"
    ;;
    --authorized-keys)
    shift
    AUTH_KEYS_FILE="${1}"
    ;;
    --help|-h)
    usage
    exit 0
    ;;
    *)
    # Unknown options are passed to emu
    EMU_ARGS+=( "$1" )
    ;;
esac
shift
done

# Check for core SDK being present
if [[ ! -d "${FUCHSIA_SDK_PATH}" ]]; then
  fx-error "Fuchsia Core SDK not found at ${FUCHSIA_SDK_PATH}."
  exit 2
fi

if [[ ! -f "${AUTH_KEYS_FILE}" ]]; then
  if [[ "${AUTH_KEYS_FILE}" == "" ]]; then
    AUTH_KEYS_FILE="${FUCHSIA_SDK_PATH}/authkeys.txt"
    # Store the SSL auth keys to a file for sending to the device.
    if ! ssh-add -L > "${AUTH_KEYS_FILE}"; then
      fx-error "Cannot determine authorized keys: $(cat "${AUTH_KEYS_FILE}")."
      exit 1
    fi
  else
    fx-error "Argument --authorized-keys was specified as ${AUTH_KEYS_FILE} but it does not exist"
    exit 1
  fi
fi

if [[ ! "$(wc -l < "${AUTH_KEYS_FILE}")" -ge 1 ]]; then
  fx-error "Cannot determine authorized keys: $(cat "${AUTH_KEYS_FILE}")."
  exit 2
fi

# Download the system images and packages
echo "Checking for system images and packages"
"${SCRIPT_SRC_DIR}/fpave.sh"  --prepare --image "${IMAGE_NAME}" --bucket "${FUCHSIA_BUCKET}" --work-dir "${FUCHSIA_IMAGE_WORK_DIR}"
"${SCRIPT_SRC_DIR}/fserve.sh" --prepare --image "${IMAGE_NAME}" --bucket "${FUCHSIA_BUCKET}" --work-dir "${FUCHSIA_IMAGE_WORK_DIR}"

# Download aemu if it is not already present
# TODO(fxb/43901): Need to handle downloading updates when aemu binaries change
echo "Checking for aemu binaries"
DOWNLOADS_DIR="${FUCHSIA_IMAGE_WORK_DIR}/emulator"
# TODO(fxb/41836): Replace hardcoded linux-amd64 with OS detection
ARCH="linux-amd64"
VER_AEMU="latest"
if [ ! -f "${DOWNLOADS_DIR}/aemu-${ARCH}-${VER_AEMU}.zip" ]; then
  mkdir -p "${DOWNLOADS_DIR}"
  echo -e "Downloading aemu archive...\c"
  curl -sL "https://chrome-infra-packages.appspot.com/dl/fuchsia/third_party/aemu/${ARCH}/+/${VER_AEMU}" -o "${DOWNLOADS_DIR}/aemu-${ARCH}-${VER_AEMU}.zip"
  echo "complete."
fi
if [ ! -d "${DOWNLOADS_DIR}/aemu-${ARCH}" ]; then
  echo -e "Extracting aemu archive...\c"
  rm -rf "${DOWNLOADS_DIR}/tmp-aemu-${ARCH}" "${DOWNLOADS_DIR}/aemu-${ARCH}"
  unzip -q "${DOWNLOADS_DIR}/aemu-${ARCH}-${VER_AEMU}.zip" -d "${DOWNLOADS_DIR}/tmp-aemu-${ARCH}"
  mv "${DOWNLOADS_DIR}/tmp-aemu-${ARCH}" "${DOWNLOADS_DIR}/aemu-${ARCH}"
  echo "complete."
fi

# Export variables needed for fx emu and fx-image-common.sh
export FUCHSIA_BUILD_DIR="${FUCHSIA_IMAGE_WORK_DIR}/image"
export PREBUILT_AEMU_DIR="${FUCHSIA_IMAGE_WORK_DIR}/emulator/aemu-linux-amd64"

# Need to make the SDK storage-full.blk writable so that the copy is writable as well, otherwise fvm extend fails in lib/fvm.sh
source "${SCRIPT_SRC_DIR}/fx-image-common.sh"
echo "Setting writable permissions on $FUCHSIA_BUILD_DIR/$IMAGE_FVM_RAW"
chmod u+w "$FUCHSIA_BUILD_DIR/$IMAGE_FVM_RAW"


"${SCRIPT_SRC_DIR}/devshell/emu" "${EMU_ARGS[@]}" -k "${AUTH_KEYS_FILE}"
