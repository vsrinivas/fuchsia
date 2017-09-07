# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/async_stub.cpp \
    $(LOCAL_DIR)/default_tests.cpp \
    $(LOCAL_DIR)/loop_tests.cpp \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/receiver_tests.cpp \
    $(LOCAL_DIR)/task_tests.cpp \
    $(LOCAL_DIR)/wait_tests.cpp \
    $(LOCAL_DIR)/wait_with_timeout_tests.cpp

MODULE_NAME := async-test

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.loop \
    system/ulib/mx \
    system/ulib/mxcpp \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/magenta \
    system/ulib/mxio \
    system/ulib/unittest

include make/module.mk
