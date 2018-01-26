# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

# Don't forget to update BUILD.gn as well for the Fuchsia build.
MODULE_SRCS += \
    $(LOCAL_DIR)/item.cpp \
    $(LOCAL_DIR)/parser.cpp

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zxcpp

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon

MODULE_PACKAGE := src

include make/module.mk
