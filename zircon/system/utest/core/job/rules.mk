# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/jobs.cpp

MODULE_NAME := job-test

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/mini-process \
    system/ulib/unittest \
    system/ulib/zircon

MODULE_STATIC_LIBS := \
    system/ulib/zx \
    system/ulib/zxcpp

include make/module.mk
