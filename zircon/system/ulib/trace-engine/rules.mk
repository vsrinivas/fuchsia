# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Three copies of libtrace-engine are built:
# 1) Shared library for use by userspace tracing.
# 2) Static library for use by userspace tracing.
# 3) Static library to be linked into libdriver.so for use by driver tracing.
#
# N.B. Please DO NOT use (2) unless you KNOW you need to. Generally you do not.
# If in doubt, ask. (2) is for very special circumstances where
# libtrace-engine.so is not available.

LOCAL_DIR := $(GET_LOCAL_DIR)

# Common pieces.

LOCAL_SRCS := \
    $(LOCAL_DIR)/context.cpp \
    $(LOCAL_DIR)/context_api.cpp \
    $(LOCAL_DIR)/engine.cpp \
    $(LOCAL_DIR)/nonce.cpp

LOCAL_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp

LOCAL_LIBS := \
    system/ulib/c \
    system/ulib/zircon

# The default version for the normal case.

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := $(LOCAL_SRCS)

MODULE_EXPORT := so
MODULE_SO_NAME := trace-engine

MODULE_STATIC_LIBS := $(LOCAL_STATIC_LIBS)

MODULE_LIBS := $(LOCAL_LIBS)

include make/module.mk

# A special version for programs and shared libraries that can't use
# libtrace-engine.so, e.g., because it is unavailable.
# N.B. Please verify that you really need this before using it.
# Generally you DO NOT want to use this.

MODULE := $(LOCAL_DIR).static
MODULE_NAME := trace-engine-static

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden
MODULE_COMPILEFLAGS += -DSTATIC_LIBRARY

MODULE_SRCS := $(LOCAL_SRCS)

MODULE_STATIC_LIBS := $(LOCAL_STATIC_LIBS)

MODULE_LIBS := $(LOCAL_LIBS)

MODULE_PACKAGE := static

include make/module.mk

# And again, but this time for drivers.
# This gets linked into libdriver.so.

MODULE := $(LOCAL_DIR).driver
MODULE_NAME := trace-engine-driver

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden
MODULE_COMPILEFLAGS += -DDDK_TRACING

MODULE_SRCS := \
    $(LOCAL_DIR)/context.cpp \
    $(LOCAL_DIR)/context_api.cpp \
    $(LOCAL_DIR)/engine.cpp \
    $(LOCAL_DIR)/nonce.cpp

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp

MODULE_LIBS := $(LOCAL_LIBS)

include make/module.mk

# Header-only src package for use by exported trace-reader package.

MODULE := $(LOCAL_DIR).headers-for-reader
MODULE_NAME := trace-engine-headers-for-reader

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_PACKAGE_SRCS := none
MODULE_PACKAGE_INCS := \
    $(LOCAL_DIR)/include/trace-engine/fields.h \
    $(LOCAL_DIR)/include/trace-engine/types.h

MODULE_STATIC_LIBS := \
    system/ulib/fbl

MODULE_LIBS :=

MODULE_PACKAGE := src

include make/module.mk
