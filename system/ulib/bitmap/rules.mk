# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/raw-bitmap.cpp \
    $(LOCAL_DIR)/rle-bitmap.cpp \

MODULE_SO_NAME := bitmap

MODULE_STATIC_LIBS := \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl \

MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/c \

MODULE_PACKAGE := shared

include make/module.mk
