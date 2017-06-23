# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/walker.cpp

MODULE_NAME := task-utils-test

MODULE_STATIC_LIBS := \
    system/ulib/mxcpp \
    system/ulib/task-utils

MODULE_LIBS := \
    system/ulib/magenta \
    system/ulib/mxio \
    system/ulib/c \
    system/ulib/unittest

include make/module.mk
