# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS := \
    $(LOCAL_DIR)/fake-bti.cpp

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zxcpp \
    system/ulib/zx \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/unittest \
    system/ulib/zircon \

MODULE_EXPORT := so
MODULE_SO_NAME := fake-bti

include make/module.mk
