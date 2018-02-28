# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM := generic-arm

include make/kernel-images.mk

PLATFORM_VID := 4   # PDEV_VID_KHADAS
PLATFORM_PID := 2   # PDEV_PID_VIM2
PLATFORM_BOARD_NAME := vim2

MDI_SRCS := $(LOCAL_DIR)/vim2.mdi
