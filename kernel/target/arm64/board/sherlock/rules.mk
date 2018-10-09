# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM_BOARD_NAME := sherlock

PLATFORM_USE_SHIM := true
PLATFORM_USE_GZIP := true
PLATFORM_DTB := kdtb
PLATFORM_USE_MKBOOTIMG := true
PLATFORM_KERNEL_OFFSET := 0x01080000
PLATFORM_MEMBASE := 0x00000000
PLATFORM_CMDLINE := netsvc.netboot=true
PLATFORM_BOOT_PARTITION_SIZE := 33554432

include make/board.mk
