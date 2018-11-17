# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Two copies of libtrace-provider are built:
# (1) Static copy that uses libtrace-engine.so (or libdriver.so for DDK).
# (2) Static copy that uses libtrace-engine.a.
#
# N.B. Please DO NOT use (2) unless you KNOW you need to. Generally you do not.
# If in doubt, ask. (2) is for very special circumstances where
# libtrace-engine.so is not available.

LOCAL_DIR := $(GET_LOCAL_DIR)

# Common pieces.

LOCAL_SRCS := \
    $(LOCAL_DIR)/handler.cpp \
    $(LOCAL_DIR)/provider_impl.cpp \
    $(LOCAL_DIR)/session.cpp \
    $(LOCAL_DIR)/trace_provider.fidl.client.cpp \
    $(LOCAL_DIR)/trace_provider.fidl.tables.cpp \
    $(LOCAL_DIR)/trace_provider.fidl.h \
    $(LOCAL_DIR)/utils.cpp

LOCAL_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/zircon-internal \
    system/ulib/zx

LOCAL_LIBS := \
    system/ulib/c \
    system/ulib/zircon \
    system/ulib/fdio

# The default version for the normal case.

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := $(LOCAL_SRCS)

MODULE_STATIC_LIBS := \
    $(LOCAL_STATIC_LIBS) \
    system/ulib/trace

MODULE_LIBS := \
    $(LOCAL_LIBS) \
    system/ulib/trace-engine

MODULE_PACKAGE := static

include make/module.mk

# A special version for programs and shared libraries that can't use
# libtrace-engine.so, e.g., because it is unavailable.
# N.B. Please verify that you really need this before using it.
# Generally you DO NOT want to use this.

MODULE := $(LOCAL_DIR).static
MODULE_NAME := trace-provider-static

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := $(LOCAL_SRCS)

MODULE_HEADER_DEPS := \
    system/ulib/trace-engine \

MODULE_STATIC_LIBS := \
    $(LOCAL_STATIC_LIBS) \
    system/ulib/trace.static \
    system/ulib/trace-engine.static

MODULE_LIBS := $(LOCAL_LIBS)

MODULE_PACKAGE := static

include make/module.mk

# For apps that use the trace engine, but not via a trace provider.
# These are usually test and benchmarking apps.
# Normal apps are not expected to use this.

MODULE := $(LOCAL_DIR).handler
MODULE_NAME := trace-handler

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS = \
    $(LOCAL_DIR)/handler.cpp

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon \
    system/ulib/trace-engine

MODULE_PACKAGE := static

include make/module.mk
