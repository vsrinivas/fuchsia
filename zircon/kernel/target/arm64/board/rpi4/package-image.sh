#!/usr/bin/env bash

# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ZIRCON_DIR="${DIR}/../../../../.."
SCRIPTS_DIR="${ZIRCON_DIR}/scripts"

ROOT_BUILD_DIR="$(pwd)"
BOARD=rpi4

"${SCRIPTS_DIR}/package-image.sh"  -r zbi -b "${BOARD}" -B "${ROOT_BUILD_DIR}" $@

# hack
BOOT_SHIM="${ROOT_BUILD_DIR}/${BOARD}-boot-shim.bin"
ZIRCON_BOOTIMAGE="${ROOT_BUILD_DIR}/zedboot.zbi"
SHIMMED_ZIRCON_BOOTIMAGE="${ZIRCON_BOOTIMAGE}.shim"

cat "${BOOT_SHIM}" "${ZIRCON_BOOTIMAGE}" > "${SHIMMED_ZIRCON_BOOTIMAGE}"
