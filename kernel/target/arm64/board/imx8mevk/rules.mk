# Copyright 2018 The Fuchsia Authors
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM_BOARD_NAME := imx8mevk
PLATFORM_USE_SHIM := true
PLATFORM_USE_MKBOOTIMG := true
PLATFORM_USE_AVB := true

PLATFORM_KERNEL_OFFSET := 0x00080000
PLATFORM_MEMBASE := 0x40000000
PLATFORM_CMDLINE := netsvc.netboot=true
PLATFORM_BOOT_PARTITION_SIZE := 33554432

include make/board.mk
