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
set -eu

SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"
# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/fuchsia-common.sh" || exit $?


VER_AEMU="$(cat "${SCRIPT_SRC_DIR}/aemu.version")"
VER_GRPCWEBPROXY="$(cat "${SCRIPT_SRC_DIR}/grpcwebproxy.version")"
ENABLE_GRPCWEBPROXY=0
PREBUILT_GRPCWEBPROXY_DIR=""
# Default architecture is x64, if the system image is arm64 then
# it requires --experiment-arm64 since there is no auto detection.
FUCHSIA_ARCH="x64"

# Export variables needed here but also in fx-image-common.sh
FUCHSIA_SDK_PATH="$(get-fuchsia-sdk-dir)"
export FUCHSIA_SDK_PATH
FUCHSIA_IMAGE_WORK_DIR="$(get-fuchsia-sdk-data-dir)"
export FUCHSIA_IMAGE_WORK_DIR
DEFAULT_FUCHSIA_IMAGE="qemu-x64"
DEFAULT_FUCHSIA_AUTHKEYS="$(get-fuchsia-auth-keys-file)"

# Download a URL $1 from CIPD, extract into directory $2
function download-extract-cipd {
  CIPD_URL="${1}"
  CIPD_DIR="${2}"
  CIPD_FILE="${2}.zip"

  if [ ! -f "${CIPD_FILE}" ]; then
    echo "Downloading from ${CIPD_URL} ..."
    curl -L "${CIPD_URL}" -o "${CIPD_FILE}" -#
    echo "Verifying download ${CIPD_FILE}"
    # CIPD will return a file containing "no such ref" if the URL is invalid, so need to check the ZIP file
    if ! unzip -qq -t "${CIPD_FILE}" &> /dev/null; then
      rm -f "${CIPD_FILE}"
      fx-error "Downloaded archive from ${CIPD_URL} failed with invalid data - the version is probably invalid"
      exit 1
    fi
    echo "Download complete."
  fi
  if [ ! -d "${CIPD_DIR}" ]; then
    echo -e "Extracting archive to ${CIPD_DIR} ..."
    rm -rf "${CIPD_DIR}" "${CIPD_DIR}-temp"
    unzip -q "${CIPD_FILE}" -d "${CIPD_DIR}-temp"
    mv "${CIPD_DIR}-temp" "${CIPD_DIR}"
    echo "Extract complete."
  else
    echo "Using existing archive in ${CIPD_DIR}"
  fi
}

emu_help () {
  # Extract command-line argument help from emu script, similar to fx-print-command-help
  sed -n -e 's/^## //p' -e 's/^##$//p' "${SCRIPT_SRC_DIR}/devshell/emu" | grep -v "usage: fx emu"
}

usage () {
  echo "Usage: $(basename "$0")"
  echo "  [--work-dir <directory to store image assets>]"
  echo "    Defaults to ${FUCHSIA_IMAGE_WORK_DIR}"
  echo "  [--bucket <fuchsia gsutil bucket>]"
  echo "    Default is read using \`fconfig.sh get emu-bucket\` if set. Otherwise defaults to ${DEFAULT_FUCHSIA_BUCKET}".
  echo "  [--image <image name>]"
  echo "     Default is read using \`fconfig.sh get emu-image\` if set. Otherwise defaults to ${DEFAULT_FUCHSIA_IMAGE}".
  echo "  [--authorized-keys <file>]"
  echo "    The authorized public key file for securing the device.  Defaults to "
  echo "    ${DEFAULT_FUCHSIA_AUTHKEYS}, which is generated if needed."
  echo "  [--version <version>]"
  echo "    Specify the CIPD version of AEMU to download."
  echo "    Defaults to aemu.version file with ${VER_AEMU}"
  echo "  [--experiment-arm64]"
  echo "    Override FUCHSIA_ARCH to arm64, instead of the default x64."
  echo "    This is required for *-arm64 system images, and is not auto detected."
  echo "  [--setup-ufw]"
  echo "    Set up ufw firewall rules needed for Fuchsia device discovery"
  echo "    and package serving, then exit. Only works on Linux with ufw"
  echo "    firewall, and requires sudo."
  echo "  [--help] [-h]"
  echo "    Show command line options for femu.sh and emu subscript"
  echo
  echo "Remaining arguments are passed to emu wrapper and emulator:"
  emu_help
  echo
  echo "Invalid argument names are not flagged as errors, and are passed on to emulator"
}
AUTH_KEYS_FILE=""
FUCHSIA_BUCKET=""
IMAGE_NAME=""
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
    --setup-ufw)
    set -xv
    if is-mac; then
      fx-error "--setup-ufw is only supported on Linux with ufw firewall"
      exit 1
    fi
    sudo ufw allow proto udp from fe80::/10 to any port 33331:33340 comment 'Fuchsia Netboot Protocol'
    sudo ufw allow proto tcp from fe80::/10 to any port 8083 comment 'Fuchsia Package Server'
    sudo ufw allow proto udp from fe80::/10 port 33340 comment 'Fuchsia Netboot TFTP Source Port'
    set +xv
    exit 0
    ;;
    --help|-h)
    usage
    exit 0
    ;;
    -x)
    shift
    ENABLE_GRPCWEBPROXY=1
    EMU_ARGS+=( -x "$1" )
    ;;
    -X)
    shift
    PREBUILT_GRPCWEBPROXY_DIR="$1"
    ;;
    --experiment-arm64)
    FUCHSIA_ARCH="arm64"
    EMU_ARGS+=( "$1" )
    ;;
    *)
    # Unknown options are passed to emu
    EMU_ARGS+=( "$1" )
    ;;
esac
shift
done

if [[ "${AUTH_KEYS_FILE}" != "" ]]; then
  auth_keys_file="${AUTH_KEYS_FILE}"
else
  auth_keys_file="${DEFAULT_FUCHSIA_AUTHKEYS}"
fi

if [[ "${FUCHSIA_BUCKET}" == "" ]]; then
  FUCHSIA_BUCKET="$(get-fuchsia-property emu-bucket)"
  if [[ "${FUCHSIA_BUCKET}" == "" ]]; then
    FUCHSIA_BUCKET="${DEFAULT_FUCHSIA_BUCKET}"
  fi
fi

if [[ "${IMAGE_NAME}" == "" ]]; then
  IMAGE_NAME="$(get-fuchsia-property emu-image)"
  if [[ "${IMAGE_NAME}" == "" ]]; then
    IMAGE_NAME="${DEFAULT_FUCHSIA_IMAGE}"
  fi
fi

# Download the system images and packages
echo "Checking for system images and packages"
"${SCRIPT_SRC_DIR}/fpave.sh"  --prepare --image "${IMAGE_NAME}" --bucket "${FUCHSIA_BUCKET}" --work-dir "${FUCHSIA_IMAGE_WORK_DIR}"
"${SCRIPT_SRC_DIR}/fserve.sh" --prepare --image "${IMAGE_NAME}" --bucket "${FUCHSIA_BUCKET}" --work-dir "${FUCHSIA_IMAGE_WORK_DIR}"

# Do not create directory names with : otherwise LD_PRELOAD usage in aemu will fail.
# Avoid / to prevent extra sub-directories being created.
LABEL_AEMU="$(echo "${VER_AEMU}" | tr ':/' '_')"
LABEL_GRPCWEBPROXY="$(echo "${VER_GRPCWEBPROXY}" | tr ':/' '_')"

# Download CIPD prebuilt binaries if not already present
DOWNLOADS_DIR="${FUCHSIA_IMAGE_WORK_DIR}/emulator"
mkdir -p "${DOWNLOADS_DIR}"
if is-mac; then
  ARCH="mac-amd64"
else
  ARCH="linux-amd64"
fi

# Export variables needed for fx emu and fx-image-common.sh
export FUCHSIA_BUILD_DIR="${FUCHSIA_IMAGE_WORK_DIR}/image"
export PREBUILT_AEMU_DIR="${DOWNLOADS_DIR}/aemu-${ARCH}-${LABEL_AEMU}"
export FUCHSIA_ARCH

download-extract-cipd \
  "https://chrome-infra-packages.appspot.com/dl/fuchsia/third_party/aemu/${ARCH}/+/${VER_AEMU}" \
  "${PREBUILT_AEMU_DIR}"

if (( ENABLE_GRPCWEBPROXY )); then
  if [[ -z "$PREBUILT_GRPCWEBPROXY_DIR" ]]; then
    PREBUILT_GRPCWEBPROXY_DIR="${DOWNLOADS_DIR}/grpcwebproxy-${ARCH}-${LABEL_GRPCWEBPROXY}"
    download-extract-cipd \
      "https://chrome-infra-packages.appspot.com/dl/fuchsia/third_party/grpcwebproxy/${ARCH}/+/${VER_GRPCWEBPROXY}" \
      "${PREBUILT_GRPCWEBPROXY_DIR}"
  fi
  EMU_ARGS+=( "-X" "${PREBUILT_GRPCWEBPROXY_DIR}" )
fi

# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/fx-image-common.sh"

if (( "${#EMU_ARGS[@]}" )); then
  "${SCRIPT_SRC_DIR}/devshell/emu" -k "${auth_keys_file}" "${EMU_ARGS[@]}"
else
  "${SCRIPT_SRC_DIR}/devshell/emu" -k "${auth_keys_file}"
fi
