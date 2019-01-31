# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

# Userspace library.

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS = \
    $(LOCAL_DIR)/reader.cpp \
    $(LOCAL_DIR)/reader_internal.cpp \
    $(LOCAL_DIR)/records.cpp

# This is for building in zircon.
MODULE_HEADER_DEPS := \
    system/ulib/trace-engine

# trace-engine.headers-for-reader is for building outsize of zircon.
# We cannot declare a dependency on trace-engine itself because then
# our users will get a libtrace-engine.so dependency. And we don't
# declare a dependency on trace-engine-static: We only need these headers.
MODULE_STATIC_LIBS := \
    system/ulib/trace-engine.headers-for-reader \
    system/ulib/zxcpp \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/c

MODULE_PACKAGE := src

include make/module.mk

# Host library.

MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS = \
    $(LOCAL_DIR)/reader.cpp \
    $(LOCAL_DIR)/records.cpp

MODULE_HEADER_DEPS := \
    system/ulib/trace-engine

MODULE_COMPILEFLAGS := \
    -Isystem/ulib/fbl/include

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib

include make/module.mk
