# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/ramdisk.cpp \

MODULE_NAME := ramdisk-test

MODULE_STATIC_LIBS := \
    system/ulib/block-client \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/fs \
    system/ulib/fzl \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fs-management \
    system/ulib/zircon \
    system/ulib/fdio \
    system/ulib/unittest \

include make/module.mk
