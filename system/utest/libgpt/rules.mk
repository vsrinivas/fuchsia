# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/libgpt-tests.cpp \

MODULE_NAME := libgpt-test

MODULE_STATIC_LIBS := \
    system/ulib/gpt \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl \
    third_party/ulib/cksum

MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/fs-management \
    system/ulib/unittest \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-hardware-ramdisk \

include make/module.mk
