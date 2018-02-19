# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_PACKAGE := static

MODULE_SRCS += \
    $(LOCAL_DIR)/item.cpp \
    $(LOCAL_DIR)/parser.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zxcpp

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon

include make/module.mk
