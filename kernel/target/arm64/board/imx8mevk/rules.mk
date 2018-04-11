# Copyright 2018 The Fuchsia Authors
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM_VID := 9   # PDEV_VID_NXP
PLATFORM_PID := 1   # PDEV_PID_IMX8MEVK
PLATFORM_BOARD_NAME := imx8mevk
PLATFORM_USE_SHIM := true

include make/board.mk
