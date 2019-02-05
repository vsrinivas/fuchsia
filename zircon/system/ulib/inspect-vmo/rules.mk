# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS += \
    $(LOCAL_DIR)/heap.cpp \
    $(LOCAL_DIR)/inspect.cpp \
    $(LOCAL_DIR)/scanner.cpp \
    $(LOCAL_DIR)/snapshot.cpp \
    $(LOCAL_DIR)/state.cpp \
    $(LOCAL_DIR)/types.cpp \

MODULE_HEADER_DEPS := \
    system/ulib/zircon-internal \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/syslog \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_PACKAGE := src

include make/module.mk

