# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM_VID := 5   # PDEV_VID_AMLOGIC
PLATFORM_PID := 5   # PDEV_PID_AMLOGIC_S905D2
PLATFORM_BOARD_NAME := aml-s905d2
PLATFORM_MDI_SRCS := $(LOCAL_DIR)/s905d2.mdi

include make/board.mk
