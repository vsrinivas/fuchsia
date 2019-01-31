# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)
MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := \
    $(LOCAL_DIR)/compare_records.cpp \
    $(LOCAL_DIR)/read_records.cpp \
    $(LOCAL_DIR)/squelch.cpp \

MODULE_HEADER_DEPS := \
    system/ulib/trace-engine

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/trace-reader \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon \

MODULE_PACKAGE := src

include make/module.mk
