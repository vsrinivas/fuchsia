# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Three copies of libtrace-provider are built:
# (1) libtrace-provider.a: Static copy that uses libtrace-engine.so
#     (or libdriver.so for DDK).
# (2) libtrace-provider.without-fdio.a: Static copy that uses
#     libtrace-engine.so but does not contain fdio support for connect to
#     trace-manager; instead the client must make its own connection.
# (3) libtrace-provider.with-static-engine.a: Static copy that uses
#     libtrace-engine.static.a.
#
# N.B. Please DO NOT use (3) unless you KNOW you need to. Generally you do not.
# If in doubt, ask. (3) is for very special circumstances where
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
    system/ulib/zircon

# The default version for the normal case.
# TODO(PT-63): Remove fdio dependency.

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := \
    $(LOCAL_SRCS) \
    $(LOCAL_DIR)/fdio_connect.cpp \
    $(LOCAL_DIR)/provider_with_fdio.cpp

MODULE_STATIC_LIBS := \
    $(LOCAL_STATIC_LIBS) \
    system/ulib/trace \
    system/ulib/trace-provider.fdio-connect

MODULE_LIBS := \
    $(LOCAL_LIBS) \
    system/ulib/fdio \
    system/ulib/trace-engine

MODULE_PACKAGE := static

include make/module.mk

# Same as the default version with the fdio dependency removed.
# TODO(PT-63): This will be removed (the default case will become this)
# when all clients are updated.

MODULE := $(LOCAL_DIR).without-fdio
MODULE_NAME := trace-provider-without-fdio

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

# A special version for programs and shared libraries that can't use
# libtrace-engine.so, e.g., because it is unavailable.
# N.B. Please verify that you really need this before using it.
# Generally you DO NOT want to use this.
# TODO(dje): Delete this version once garnet is updated.

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

# A special version for programs and shared libraries that can't use
# libtrace-engine.so, e.g., because it is unavailable.
# N.B. Please verify that you really need this before using it.
# Generally you DO NOT want to use this.

MODULE := $(LOCAL_DIR).with-static-engine
MODULE_NAME := trace-provider-with-static-engine

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := $(LOCAL_SRCS)

MODULE_HEADER_DEPS := \
    system/ulib/trace-engine \

MODULE_STATIC_LIBS := \
    $(LOCAL_STATIC_LIBS) \
    system/ulib/trace.with-static-engine \
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

# A helper library for clients that want to use fdio to connect to
# trace manager.

MODULE := $(LOCAL_DIR).fdio-connect
MODULE_NAME := trace-provider-fdio-connect

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS = \
    $(LOCAL_DIR)/fdio_connect.cpp \
    $(LOCAL_DIR)/provider_with_fdio.cpp

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/fbl \
    system/ulib/zx

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon \
    system/ulib/fdio

MODULE_PACKAGE := static

include make/module.mk
