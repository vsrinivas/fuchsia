# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

# Common pieces.

LOCAL_SRCS := \
    $(LOCAL_DIR)/event.cpp \
    $(LOCAL_DIR)/observer.cpp

LOCAL_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/fbl \
    system/ulib/zx

LOCAL_LIBS := \
    system/ulib/c \
    system/ulib/zircon

# The default version for the normal case.

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := $(LOCAL_SRCS)

MODULE_STATIC_LIBS := $(LOCAL_STATIC_LIBS)

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
MODULE_NAME := trace-with-static-engine

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := $(LOCAL_SRCS)

MODULE_HEADER_DEPS := \
    system/ulib/trace-engine \

MODULE_STATIC_LIBS := \
    $(LOCAL_STATIC_LIBS) \
    system/ulib/trace-engine.static

MODULE_LIBS := $(LOCAL_LIBS)

MODULE_PACKAGE := static

include make/module.mk

# And again, but this time for drivers.
# This gets linked into each driver.

MODULE := $(LOCAL_DIR).driver
MODULE_NAME := trace-driver

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := $(LOCAL_SRCS)

MODULE_HEADER_DEPS := \
    system/ulib/trace-engine

MODULE_STATIC_LIBS := \
    $(LOCAL_STATIC_LIBS) \
    system/ulib/trace-engine.driver

MODULE_LIBS := $(LOCAL_LIBS)

include make/module.mk
