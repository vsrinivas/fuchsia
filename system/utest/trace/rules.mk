# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

# Common pieces.

LOCAL_SRCS := \
    $(LOCAL_DIR)/engine_tests.cpp \
    $(LOCAL_DIR)/event_tests_ntrace.c \
    $(LOCAL_DIR)/event_tests_ntrace.cpp \
    $(LOCAL_DIR)/event_tests.c \
    $(LOCAL_DIR)/event_tests.cpp \
    $(LOCAL_DIR)/fields_tests.cpp \
    $(LOCAL_DIR)/fixture.cpp \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/no_optimization.c \
    $(LOCAL_DIR)/record_tests.cpp

LOCAL_HEADER_DEPS := \
    system/ulib/trace-provider

LOCAL_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async-loop.cpp \
    system/ulib/async-loop \
    system/ulib/fbl \
    system/ulib/trace-provider.handler \
    system/ulib/trace-reader \
    system/ulib/trace-test-utils \
    system/ulib/zx \
    system/ulib/zxcpp \

# fdio is here so that things like printf work.
# Otherwise they silently fail (output is dropped).
LOCAL_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

# Version of test that uses libtrace-engine.so.

MODULE := $(LOCAL_DIR)
MODULE_NAME := trace-test

MODULE_TYPE := usertest

MODULE_SRCS := $(LOCAL_SRCS)

MODULE_HEADER_DEPS := $(LOCAL_HEADER_DEPS)

MODULE_STATIC_LIBS := \
    $(LOCAL_STATIC_LIBS) \
    system/ulib/trace

MODULE_LIBS := \
    $(LOCAL_LIBS) \
    system/ulib/trace-engine

include make/module.mk

# And again using libtrace-engine-static.a.

MODULE := $(LOCAL_DIR).static
MODULE_NAME := trace-static-test

MODULE_TYPE := usertest

MODULE_SRCS := $(LOCAL_SRCS)

MODULE_HEADER_DEPS := \
    $(LOCAL_HEADER_DEPS) \
    system/ulib/trace \
    system/ulib/trace-engine

MODULE_STATIC_LIBS := \
    $(LOCAL_STATIC_LIBS) \
    system/ulib/trace.static \
    system/ulib/trace-engine.static

MODULE_LIBS := $(LOCAL_LIBS)

include make/module.mk
