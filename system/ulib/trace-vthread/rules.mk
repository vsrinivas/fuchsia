# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := \
    $(LOCAL_DIR)/event_vthread.cpp

MODULE_HEADER_DEPS := \
    system/ulib/trace-engine

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/trace

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon

MODULE_PACKAGE := static

include make/module.mk

# And the unittest.

MODULE := $(LOCAL_DIR).test
MODULE_NAME := trace-vthread-test

MODULE_TYPE := usertest

MODULE_SRCS := \
    $(LOCAL_DIR)/event_vthread_tests.cpp

MODULE_HEADER_DEPS := \
    system/ulib/trace-provider

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async-loop.cpp \
    system/ulib/async-loop \
    system/ulib/fbl \
    system/ulib/trace \
    system/ulib/trace-provider.handler \
    system/ulib/trace-reader \
    system/ulib/trace-test-utils \
    system/ulib/trace-vthread \
    system/ulib/zx \
    system/ulib/zxcpp

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/trace-engine \
    system/ulib/unittest \
    system/ulib/zircon

include make/module.mk
