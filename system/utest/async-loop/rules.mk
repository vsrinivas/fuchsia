# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/loop_tests.cpp \
    $(LOCAL_DIR)/main.c \

MODULE_NAME := async-loop-test

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async-loop.cpp \
    system/ulib/async-loop \
    system/ulib/async-testutils \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/zircon \
    system/ulib/fdio \
    system/ulib/unittest

include make/module.mk
