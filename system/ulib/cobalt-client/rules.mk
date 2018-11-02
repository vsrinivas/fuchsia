# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS += \
    $(LOCAL_DIR)/cobalt_logger.cpp \
    $(LOCAL_DIR)/collector.cpp \
    $(LOCAL_DIR)/counter.cpp \
    $(LOCAL_DIR)/histogram.cpp \
    $(LOCAL_DIR)/event_buffer.cpp \
    $(LOCAL_DIR)/metric_info.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/fzl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-cobalt \
    system/fidl/fuchsia-mem \

MODULE_PACKAGE := src

include make/module.mk
