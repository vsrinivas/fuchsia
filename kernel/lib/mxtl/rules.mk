# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

SRC_DIR := system/ulib/mxtl
LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

KERNEL_INCLUDES += $(LOCAL_DIR)/include \
                   $(SRC_DIR)/include

MODULE_SRCS := \
    $(LOCAL_DIR)/arena.cpp \
    $(LOCAL_DIR)/arena_tests.cpp \
    $(LOCAL_DIR)/inline_array_tests.cpp \

include make/module.mk
