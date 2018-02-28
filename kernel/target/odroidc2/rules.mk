# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM := generic-arm

PLATFORM_VID := 3   # PDEV_VID_AMLOGIC
PLATFORM_PID := 1   # PDEV_PID_AMLOGIC_S905
PLATFORM_BOARD_NAME := odroid-c2

# build MDI
MDI_SRCS := $(LOCAL_DIR)/odroidc2.mdi
