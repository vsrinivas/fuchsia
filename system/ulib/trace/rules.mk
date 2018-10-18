# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := \
    $(LOCAL_DIR)/event.cpp \
    $(LOCAL_DIR)/observer.cpp

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/zx \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon \
    system/ulib/trace-engine

MODULE_PACKAGE := src

include make/module.mk

# And again, but this time for drivers.
# This gets linked into each driver.

MODULE := $(LOCAL_DIR).driver
MODULE_NAME := trace-driver

MODULE_TYPE := userlib
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := \
    $(LOCAL_DIR)/event.cpp \
    $(LOCAL_DIR)/observer.cpp

MODULE_HEADER_DEPS := \
    system/ulib/trace-engine

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/fbl \
    system/ulib/trace-engine.driver \
    system/ulib/zx

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon

include make/module.mk
