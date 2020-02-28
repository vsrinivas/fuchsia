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
ZIRCON_TOOLS_DIR="${FUCHSIA_SDK_PATH}/tools"
HOST_OUT_DIR="${FUCHSIA_SDK_PATH}/tools"
IMAGE_ZIRCONA_ZBI="zircon-a.zbi"
IMAGE_QEMU_KERNEL_RAW="qemu-kernel.kernel"
IMAGE_FVM_RAW="storage-full.blk"
# TODO(fxb/43807): Replace FUCHSIA_ARCH with detecting the architecture, currently only tested with *-x64 images
FUCHSIA_ARCH="x64"

# Provide fx-zbi functionality using the SDK zbi tool
function fx-zbi {
  "${ZIRCON_TOOLS_DIR}/zbi" "$@"
}
