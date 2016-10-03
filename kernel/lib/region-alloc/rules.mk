# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

SRC_DIR := system/ulib/region-alloc
LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)

KERNEL_INCLUDES += $(SRC_DIR)/include

MODULE_SRCS := \
    $(SRC_DIR)/region-alloc-c-api.cpp \
    $(SRC_DIR)/region-alloc.cpp

MODULE_COMPILEFLAGS := -Wno-error=null-dereference -Wno-null-dereference

include make/module.mk

