# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM_VID := 2   # PDEV_VID_96BOARDS
PLATFORM_PID := 1   # PDEV_PID_HIKEY960
PLATFORM_BOARD_NAME := hikey960
PLATFORM_USE_SHIM := true

include make/board.mk
