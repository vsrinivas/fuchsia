#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Command to pave a Fuchsia device.

# note: set -e is not used in order to have custom error handling.
set -u

# Source common functions
SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"

# Fuchsia command common functions.
# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/fuchsia-common.sh" || exit $?

FUCHSIA_IMAGE_WORK_DIR="$(get-fuchsia-sdk-data-dir)"

DEVICE_NAME_FILTER="$(get-fuchsia-property device-name)"

FUCHSIA_BUCKET="$(get-fuchsia-property bucket)"
if [[ "${FUCHSIA_BUCKET}" == "" ]]; then
  FUCHSIA_BUCKET="${DEFAULT_FUCHSIA_BUCKET}"
fi

IMAGE_NAME="$(get-fuchsia-property image)"
if [[ "${IMAGE_NAME}" == "" ]]; then
  IMAGE_NAME="generic-x64"
fi

function usage {
  echo "Usage: $0"
  echo "  [--image <image name>"]
  echo "    Defaults to ${IMAGE_NAME}. Use --image list to list all available images."
  echo "  [--work-dir <working directory to store image assets>]"
  echo "    Defaults to ${FUCHSIA_IMAGE_WORK_DIR}."
  echo "  [--bucket <fuchsia gsutil bucket>]"
  echo "    Defaults to ${FUCHSIA_BUCKET}."
  echo "  [--authorized-keys <file>]"
  echo "    The authorized public key file for securing the device.  Defaults to "
  echo "    ${HOME}/.ssh/fuchsia_authorized_keys, which is generated if needed."
  echo "  [--private-key <identity file>]"
  echo "    Uses additional private key when using ssh to access the device."
  echo "  [--device-name <device hostname>]"
  echo "    Only paves a device with the given device hostname."
  if [[ "${DEVICE_NAME_FILTER}" != "" ]]; then
    echo "    Defaults to ${DEVICE_NAME_FILTER}."
  fi
  echo "  [--prepare]"
  echo "    Downloads any dependencies but does not pave to a device."
  echo "  [--zedboot]"
  echo "    Updates the Zedboot bootloader and exits."
  echo "  [-x] Enable debug."
}

PRIVATE_KEY_FILE=""
PREPARE_ONLY=""
AUTH_KEYS_FILE=""
UPDATE_ZEDBOOT=""


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
    --private-key)
      shift
      PRIVATE_KEY_FILE="${1}"
    ;;
    --device-name)
      shift
      DEVICE_NAME_FILTER="${1}"
    ;;
    --prepare)
      PREPARE_ONLY="yes"
    ;;
    --zedboot)
      UPDATE_ZEDBOOT="yes"
    ;;
    -x)
      set -x
    ;;
    *)
    # unknown option
    fx-error "Unknown option $1."
    usage
    exit 1
    ;;
esac
shift
done


if ! SDK_ID="$(get-sdk-version)"; then
  fx-error "Could not get SDK version"
  exit 1
fi

if [[ "${IMAGE_NAME}" == "list" ]]; then
  if ! IMAGES="$(get-available-images "${SDK_ID}" "${FUCHSIA_BUCKET}")"; then
    fx-error "Could not get list of available images for ${SDK_ID}"
    exit 1
  fi
  echo "Valid images for this SDK version are: ${IMAGES}."
  exit 1
fi

FUCHSIA_TARGET_IMAGE="$(get-image-src-path "${SDK_ID}" "${IMAGE_NAME}")"
# The image tarball, we add the SDK ID to make it unique, and note the
# .tar.gz extension for packages vs .tgz extension for images.
IMAGE_FILENAME="${SDK_ID}_${IMAGE_NAME}.tgz"

echo "Paving ${IMAGE_NAME} system image for SDK version $(get-sdk-version) from ${FUCHSIA_TARGET_IMAGE}"

# Validate the image is found
if [[ ! -f "${FUCHSIA_IMAGE_WORK_DIR}/${IMAGE_FILENAME}" ]] ; then
  if ! run-gsutil ls "${FUCHSIA_TARGET_IMAGE}" >/dev/null 2>&1; then
    echo "Image ${IMAGE_NAME} not found. Valid images for this SDK version are:"
    if ! IMAGES="$(get-available-images "${SDK_ID}" "${FUCHSIA_BUCKET}")"; then
      fx-error "Could not get list of available images for ${SDK_ID}"
      exit 1
    fi
    echo "${IMAGES}"
    exit 2
  fi

  if ! run-gsutil cp "${FUCHSIA_TARGET_IMAGE}" "${FUCHSIA_IMAGE_WORK_DIR}/${IMAGE_FILENAME}"; then
    fx-error "Could not copy image from ${FUCHSIA_TARGET_IMAGE} to ${FUCHSIA_IMAGE_WORK_DIR}/${IMAGE_FILENAME}."
    exit 2
  fi
else
  echo "Skipping download, image exists."
fi

# The checksum file contains the output from `md5`. This is used to detect content
# changes in the image file.
CHECKSUM_FILE="${FUCHSIA_IMAGE_WORK_DIR}/image/image.md5"

# check that any existing contents of the image directory match the intended target device
if [[ -f "${CHECKSUM_FILE}" ]]; then
  if [[ $(run-md5 "${FUCHSIA_IMAGE_WORK_DIR}/${IMAGE_FILENAME}") != "$(< "${CHECKSUM_FILE}")" ]]; then
    fx-warn "Removing old image files."
    if ! rm -f "$(cut -d ' ' -f3 "${CHECKSUM_FILE}")"; then
      fx-error "Could not clean up old image archive."
      exit 2
    fi
    if ! rm -rf "${FUCHSIA_IMAGE_WORK_DIR}/image"; then
      fx-error "Could not clean up old image."
      exit 2
    fi
  fi
else
  # if the checksum file does not exist, something is inconsistent.
  # so delete the entire directory to make sure we're starting clean.
  # This also happens on a clean run, where the image directory does not
  # exist.
  if ! rm -rf "${FUCHSIA_IMAGE_WORK_DIR}/image"; then
    fx-error "Could not clean up old image."
    exit 2
  fi
fi

if ! mkdir -p "${FUCHSIA_IMAGE_WORK_DIR}/image"; then
  fx-error "Could not create image directory."
  exit 2
fi

# if the tarball is not untarred, do it.
if [[ ! -f "${FUCHSIA_IMAGE_WORK_DIR}/image/pave.sh" ]]; then
  if !  tar xzf "${FUCHSIA_IMAGE_WORK_DIR}/${IMAGE_FILENAME}" --directory "${FUCHSIA_IMAGE_WORK_DIR}/image"; then
    fx-error "Could not extract image from ${FUCHSIA_IMAGE_WORK_DIR}/${IMAGE_FILENAME}."
    exit 1
  fi
  run-md5 "${FUCHSIA_IMAGE_WORK_DIR}/${IMAGE_FILENAME}" > "${CHECKSUM_FILE}"
fi

# Exit out if we only need to prepare the downloads
if [[ "${PREPARE_ONLY}" == "yes" ]]; then
  exit 0
fi

SSH_ARGS=()

if [[ "${PRIVATE_KEY_FILE}" != "" ]]; then
  SSH_ARGS+=( "-i" "${PRIVATE_KEY_FILE}")
fi

# Get the device IP address.  If we can't find it, it could be at the zedboot
# page, so it is not fatal.
DEVICE_IP=$(get-device-ip-by-name "$DEVICE_NAME_FILTER")
if [[ "$?" && -n "$DEVICE_IP" ]]; then
    SSH_ARGS+=( "${DEVICE_IP}" dm reboot-recovery )
    if ! ssh-cmd "${SSH_ARGS[@]}"; then
      fx-warn "Confirm device is rebooting into recovery mode.  Paving may fail if device is not in Zedboot."
    fi
else
    fx-warn "Device not detected.  Make sure the device is connected and at the 'Zedboot' screen."
fi

# The prebuilt images do not have a bootserver for mac, so overwrite the bootserver with the
# mac bootserver. See fxbug.dev/48346.
if is-mac; then
  cp -f "$(get-fuchsia-sdk-tools-dir)/bootserver" "${FUCHSIA_IMAGE_WORK_DIR}/image/bootserver.exe.linux-x64"
fi


if [[ "${UPDATE_ZEDBOOT}" == "yes" ]]; then
  PAVE_CMD=("${FUCHSIA_IMAGE_WORK_DIR}/image/pave-zedboot.sh")
else
  PAVE_CMD=("${FUCHSIA_IMAGE_WORK_DIR}/image/pave.sh")
fi

if [[ "${AUTH_KEYS_FILE}" != "" ]]; then
  auth_keys_file="${AUTH_KEYS_FILE}"
else
  auth_keys_file="$(get-fuchsia-auth-keys-file)"
fi

PAVE_CMD+=("--authorized-keys" "${auth_keys_file}" "-1")
if ! "${PAVE_CMD[@]}"; then
  # Currently there is a bug on the first attempt of paving, so retry.
  sleep .33
  if ! "${PAVE_CMD[@]}"; then
    exit 2
  fi
fi
