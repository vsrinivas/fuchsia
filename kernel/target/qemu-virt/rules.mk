# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

KERNEL_INCLUDES += \
    $(LOCAL_DIR)/include

PLATFORM := generic-arm

PLATFORM_VID := 1   # PDEV_VID_QEMU
PLATFORM_PID := 1   # PDEV_PID_QEMU
PLATFORM_BOARD_NAME := qemu-virt

MDI_SRCS += $(LOCAL_DIR)/qemu.mdi
