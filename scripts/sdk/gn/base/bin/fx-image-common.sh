#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Requires FUCHSIA_IMAGE_WORK_DIR and FUCHSIA_SDK_PATH to be defined

# fx commands require environment variables to be defined
if [[ "${FUCHSIA_IMAGE_WORK_DIR}" == "" ]]; then
  fx-error "FUCHSIA_IMAGE_WORK_DIR must be defined before sourcing this script"
  exit 1
fi
if [[ "${FUCHSIA_SDK_PATH}" == "" ]]; then
  fx-error "FUCHSIA_SDK_PATH must be defined before sourcing this script"
  exit 1
fi

# Variables expected by fx emu
ZIRCON_TOOLS_DIR="$(get-fuchsia-sdk-tools-dir)"
# shellcheck disable=SC2034
FUCHSIA_DIR="${FUCHSIA_SDK_PATH}/bin"
# shellcheck disable=SC2034
HOST_OUT_DIR="$(get-fuchsia-sdk-tools-dir)"
# shellcheck disable=SC2034
IMAGE_ZIRCONA_ZBI="zircon-a.zbi"
# shellcheck disable=SC2034
IMAGE_QEMU_KERNEL_RAW="qemu-kernel.kernel"
# shellcheck disable=SC2034
IMAGE_FVM_FOR_EMU="storage-full.blk"

# Provide fx-zbi functionality using the SDK zbi tool
function fx-zbi {
  "${ZIRCON_TOOLS_DIR}/zbi" "$@"
}
