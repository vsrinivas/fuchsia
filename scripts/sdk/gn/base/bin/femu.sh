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
# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/fuchsia-common.sh" || exit $?
FUCHSIA_BUCKET="${DEFAULT_FUCHSIA_BUCKET}"
IMAGE_NAME="qemu-x64"
VER_AEMU="$(cat "${SCRIPT_SRC_DIR}/aemu.version")"

# Export variables needed here but also in femu.sh
FUCHSIA_SDK_PATH="$(get-fuchsia-sdk-dir)"
export FUCHSIA_SDK_PATH
FUCHSIA_IMAGE_WORK_DIR="$(get-fuchsia-sdk-data-dir)"
export FUCHSIA_IMAGE_WORK_DIR

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
  echo "  [--version <version>"
  echo "    Specify the CIPD version of AEMU to download."
  echo "    Defaults to aemu.version file with ${VER_AEMU}"
  echo "  [--help] [-h]"
  echo "    Show command line options for femu.sh and emu subscript"
  echo
  echo "Remaining arguments are passed to emu wrapper and emulator:"
  emu_help
  echo
  echo "Invalid argument names are not flagged as errors, and are passed on to emulator"
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
    --version)
    shift
    VER_AEMU="${1}"
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


if [[ "${AUTH_KEYS_FILE}" == "" ]]; then
  AUTH_KEYS_FILE="$(get-fuchsia-sdk-dir)/authkeys.txt"
  if [[ ! -f "${AUTH_KEYS_FILE}" ]]; then
    # Store the SSL auth keys to a file for sending to the device.
    if ! ssh-add -L > "${AUTH_KEYS_FILE}"; then
      fx-error "Cannot determine authorized keys: $(cat "${AUTH_KEYS_FILE}")."
      exit 1
    fi
  fi
elif [[ ! -f "${AUTH_KEYS_FILE}" ]]; then
    fx-error "Argument --authorized-keys was specified as ${AUTH_KEYS_FILE} but it does not exist"
    exit 1
fi

if [[ ! "$(wc -l < "${AUTH_KEYS_FILE}")" -ge 1 ]]; then
  fx-error "Cannot determine authorized keys: $(cat "${AUTH_KEYS_FILE}")."
  exit 2
fi

# Download the system images and packages
echo "Checking for system images and packages"
"${SCRIPT_SRC_DIR}/fpave.sh"  --prepare --image "${IMAGE_NAME}" --bucket "${FUCHSIA_BUCKET}" --work-dir "${FUCHSIA_IMAGE_WORK_DIR}"
"${SCRIPT_SRC_DIR}/fserve.sh" --prepare --image "${IMAGE_NAME}" --bucket "${FUCHSIA_BUCKET}" --work-dir "${FUCHSIA_IMAGE_WORK_DIR}"

# Do not create directory names with : otherwise LD_PRELOAD usage in aemu will fail.
# Avoid / to prevent extra sub-directories being created.
LABEL_AEMU="$(echo "${VER_AEMU}" | tr ':/' '_')"

# Download aemu binaries for the aemu.version if not already present
DOWNLOADS_DIR="${FUCHSIA_IMAGE_WORK_DIR}/emulator"
# TODO(fxb/41836): Replace hardcoded linux-amd64 with OS detection
ARCH="linux-amd64"
CIPD_FILE="${DOWNLOADS_DIR}/aemu-${ARCH}-${LABEL_AEMU}.zip"
if [ ! -f "${CIPD_FILE}" ]; then
  mkdir -p "${DOWNLOADS_DIR}"
  CIPD_URL="https://chrome-infra-packages.appspot.com/dl/fuchsia/third_party/aemu/${ARCH}/+/${VER_AEMU}"
  echo "Downloading aemu archive from ${CIPD_URL} ..."
  curl -L "${CIPD_URL}" -o "${CIPD_FILE}" -#
  echo "Verifying aemu download ${CIPD_FILE}"
  # CIPD will return a file containing "no such ref" if the URL is invalid, so need to check the ZIP file
  if ! unzip -qq -t "${CIPD_FILE}" &> /dev/null; then
    rm -f "${CIPD_FILE}"
    fx-error "Downloaded archive from ${CIPD_URL} failed with invalid data - the version ${VER_AEMU} is probably invalid"
    exit 1
  fi
  echo "Download complete."
fi
CIPD_DIR="${DOWNLOADS_DIR}/aemu-${ARCH}-${LABEL_AEMU}"
if [ ! -d "${CIPD_DIR}" ]; then
  echo -e "Extracting aemu archive to ${CIPD_DIR} ..."
  rm -rf "${DOWNLOADS_DIR}/tmp-aemu-${ARCH}-${LABEL_AEMU}" "${CIPD_DIR}"
  unzip -q "${CIPD_FILE}" -d "${CIPD_DIR}-temp"
  mv "${CIPD_DIR}-temp" "${CIPD_DIR}"
  echo "Extract complete."
else
  echo "Using existing aemu archive in ${CIPD_DIR}"
fi

# Export variables needed for fx emu and fx-image-common.sh
export FUCHSIA_BUILD_DIR="${FUCHSIA_IMAGE_WORK_DIR}/image"
export PREBUILT_AEMU_DIR="${CIPD_DIR}"

# Need to make the SDK storage-full.blk writable so that the copy is writable as well, otherwise fvm extend fails in lib/fvm.sh
# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/fx-image-common.sh"
echo "Setting writable permissions on $FUCHSIA_BUILD_DIR/$IMAGE_FVM_RAW"
chmod u+w "$FUCHSIA_BUILD_DIR/$IMAGE_FVM_RAW"


"${SCRIPT_SRC_DIR}/devshell/emu" -k "${AUTH_KEYS_FILE}" "${EMU_ARGS[@]}"
