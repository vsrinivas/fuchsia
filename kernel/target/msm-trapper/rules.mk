# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM := generic-arm

DEVICE_TREE := $(GET_LOCAL_DIR)/device-tree.dtb

# extra build rules for building fastboot compatible image
include make/fastboot.mk

# build MDI
MDI_SRCS := \
    $(LOCAL_DIR)/trapper.mdi \

MDI_DEPS := \
    kernel/include/mdi/kernel-defs.mdi \

include make/mdi.mk
