# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/engine_tests.cpp \
    $(LOCAL_DIR)/event_tests_ntrace.c \
    $(LOCAL_DIR)/event_tests_ntrace.cpp \
    $(LOCAL_DIR)/event_tests.c \
    $(LOCAL_DIR)/event_tests.cpp \
    $(LOCAL_DIR)/fields_tests.cpp \
    $(LOCAL_DIR)/fixture.cpp \
    $(LOCAL_DIR)/main.c

MODULE_NAME := trace-test

MODULE_STATIC_LIBS := \
    system/ulib/trace \
    system/ulib/trace-reader \
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
    system/ulib/trace-engine \
    system/ulib/unittest

include make/module.mk
