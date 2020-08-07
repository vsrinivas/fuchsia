#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
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
# Computed from instance id at https://chrome-infra-packages.appspot.com/p/fuchsia_internal/gui_tools/fuchsia_devtools/linux-amd64/+/dogfood-1-1
VER_DEVTOOLS="$(cat "${SCRIPT_SRC_DIR}/devtools.version")"

FUCHSIA_IMAGE_WORK_DIR="$(get-fuchsia-sdk-data-dir)"
export FUCHSIA_IMAGE_WORK_DIR


usage () {
  echo "Usage: $0"
  echo "  [--work-dir <directory to store image assets>]"
  echo "    Defaults to ${FUCHSIA_IMAGE_WORK_DIR}"
  echo "  [--private-key <identity file>]"
  echo "    Uses additional private key when using ssh to access the device."
  echo "  [--version <version>"
  echo "    Specify the CIPD version of DevTools to download."
  echo "    Defaults to devtools.version file with ${VER_DEVTOOLS}"
  echo "  [--help] [-h]"
  echo "    Show command line options available"
}

PRIVATE_KEY_FILE=""
FDT_ARGS=()

# Parse command line
while (( "$#" )); do
case $1 in
  --work-dir)
    shift
    FUCHSIA_IMAGE_WORK_DIR="${1:-.}"
    ;;
  --private-key)
    shift
    PRIVATE_KEY_FILE="${1}"
    ;;
  --version)
    shift
    VER_DEVTOOLS="${1}"
    ;;
  --help|-h)
    usage
    exit 0
    ;;
  *)
    # Unknown options are passed to Fuchsia DevTools
    FDT_ARGS+=( "$1" )
    ;;
esac
shift
done

readonly PRIVATE_KEY_FILE

# Do not create directory names with : otherwise LD_PRELOAD or PATH usage will fail.
# Avoid / to prevent extra sub-directories being created.
LABEL_DEVTOOLS="$(echo "${VER_DEVTOOLS}" | tr ':/' '_')"

# Can download Fuchsia DevTools from CIPD with either "latest" or a CIPD hash
echo "Downloading Fuchsia DevTools ${VER_DEVTOOLS} with CIPD"
TEMP_ENSURE=$(mktemp /tmp/fuchsia_devtools_cipd.ensure.XXXXXX)
cat << end > "${TEMP_ENSURE}"
\$ServiceURL https://chrome-infra-packages.appspot.com/
fuchsia_internal/gui_tools/fuchsia_devtools/\${platform} $VER_DEVTOOLS
end

FDT_DIR="${FUCHSIA_IMAGE_WORK_DIR}/fuchsia_devtools-${LABEL_DEVTOOLS}"
if ! run-cipd ensure -ensure-file "${TEMP_ENSURE}" -root "${FDT_DIR}"; then
  rm "$TEMP_ENSURE"
  echo "Failed to download Fuchsia DevTools ${VER_DEVTOOLS}."
  exit 1
fi
rm "${TEMP_ENSURE}"

export FDT_TOOLCHAIN="GN"
FDT_GN_SSH="$(command -v ssh)"
export FDT_GN_SSH
FDT_SSH_CONFIG="$(get-fuchsia-sshconfig-file)"
export FDT_SSH_CONFIG
FDT_GN_DEVFIND="$(get-fuchsia-sdk-tools-dir)/device-finder"
export FDT_GN_DEVFIND
export FDT_DEBUG="true"
if [[ "${PRIVATE_KEY_FILE}" != "" ]]; then
  FDT_SSH_KEY="${PRIVATE_KEY_FILE}"
  export FDT_SSH_KEY
fi
echo "Starting Fuchsia DevTools with FDT_ARGS=[${FDT_ARGS[*]}] and environment:"
env | grep FDT_

LINUX_BINARY="system_monitor/linux/fuchsia_devtools"
LINUX_BINARY_OLD="system_monitor/linux/system_monitor"
MAC_ZIP="fuchsia_devtools/macos/macos.zip"
MAC_UNZIP_DIR="fuchsia_devtools/macos-extracted"
MAC_BINARY="fuchsia_devtools/macos-extracted/Fuchsia DevTools.app"

if is-mac; then
  if [[ ! -d "${FDT_DIR}/${MAC_UNZIP_DIR}" ]]; then
    if ! unzip -qq "${FDT_DIR}/${MAC_ZIP}" -d "${FDT_DIR}/${MAC_UNZIP_DIR}-temp"; then
      rm -rf "${FDT_DIR}/${MAC_UNZIP_DIR}-temp"
      fx-error "Downloaded archive for ${LABEL_DEVTOOLS} failed to extract"
      exit 1
    fi
    mv "${FDT_DIR}/${MAC_UNZIP_DIR}-temp" "${FDT_DIR}/${MAC_UNZIP_DIR}"
  fi
  open "${FDT_DIR}/${MAC_BINARY}" "--args" "${FDT_ARGS[@]}"
else
  if [[ -x "${FDT_DIR}/${LINUX_BINARY}" ]]; then
    "${FDT_DIR}/${LINUX_BINARY}" "${FDT_ARGS[@]}"
  elif [[ -x "${FDT_DIR}/${LINUX_BINARY_OLD}" ]]; then
    "${FDT_DIR}/${LINUX_BINARY_OLD}" "${FDT_ARGS[@]}"
  else
    echo "Failed to find Fuchsia DevTools binary."
    exit 1
  fi
fi
