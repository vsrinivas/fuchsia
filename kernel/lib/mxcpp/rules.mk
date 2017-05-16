# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

SRC_DIR := system/ulib/mxcpp
LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS := \
    $(SRC_DIR)/new.cpp \
    $(SRC_DIR)/pure_virtual.cpp \

KERNEL_INCLUDES += $(SRC_DIR)/include

include make/module.mk
