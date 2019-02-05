# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO(MA-488): Add ddk support.

LOCAL_DIR := $(GET_LOCAL_DIR)

# Common pieces.

LOCAL_SRCS := \
    $(LOCAL_DIR)/event_vthread.cpp

# The build system doesn't handle transitive dependencies, fbl is from
# trace-engine.
LOCAL_HEADER_DEPS := \
    system/ulib/fbl

LOCAL_LIBS := \
    system/ulib/c \
    system/ulib/zircon

# The default version for the normal case.

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := $(LOCAL_SRCS)

MODULE_HEADER_DEPS := \
    $(LOCAL_HEADER_DEPS)

MODULE_STATIC_LIBS := \
    system/ulib/trace

MODULE_LIBS := \
    $(LOCAL_LIBS) \
    system/ulib/trace-engine

MODULE_PACKAGE := static

include make/module.mk

# A special version for programs and shared libraries that can't use
# libtrace-engine.so.
# N.B. Please verify that you really need this before using it.
# Generally you DON'T want to use this.

MODULE := $(LOCAL_DIR).with-static-engine
MODULE_NAME := trace-vthread-with-static-engine

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := $(LOCAL_SRCS)

MODULE_HEADER_DEPS := \
    $(LOCAL_HEADER_DEPS) \
    system/ulib/trace \
    system/ulib/trace-engine

MODULE_STATIC_LIBS := \
    system/ulib/trace.with-static-engine \
    system/ulib/trace-engine.static

MODULE_LIBS := $(LOCAL_LIBS)

MODULE_PACKAGE := static

include make/module.mk

# Common pieces of the unittest.

LOCAL_TEST_SRCS := \
    $(LOCAL_DIR)/event_vthread_tests.cpp

LOCAL_TEST_HEADER_DEPS := \
    system/ulib/trace-provider

LOCAL_TEST_STATIC_LIBS := \
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

LOCAL_TEST_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon

# The unittest for the default case of dynamically linked trace-engine.

MODULE := $(LOCAL_DIR).test
MODULE_NAME := trace-vthread-test

MODULE_TYPE := usertest

MODULE_SRCS := $(LOCAL_TEST_SRCS)

MODULE_HEADER_DEPS := $(LOCAL_TEST_HEADER_DEPS)

MODULE_STATIC_LIBS := $(LOCAL_TEST_STATIC_LIBS)

MODULE_LIBS := \
    $(LOCAL_TEST_LIBS) \
    system/ulib/trace-engine

include make/module.mk

# The unittest with a static trace-engine.

MODULE := $(LOCAL_DIR).with-static-engine-test
MODULE_NAME := trace-vthread-with-static-engine-test

MODULE_TYPE := usertest

MODULE_SRCS := $(LOCAL_TEST_SRCS)

MODULE_HEADER_DEPS := \
    $(LOCAL_TEST_HEADER_DEPS) \
    system/ulib/trace-engine

MODULE_STATIC_LIBS := \
    $(LOCAL_TEST_STATIC_LIBS) \
    system/ulib/trace-engine.static

MODULE_LIBS := $(LOCAL_TEST_LIBS)

include make/module.mk
