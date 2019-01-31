# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS = \
    $(LOCAL_DIR)/fx_logger.h \
    $(LOCAL_DIR)/fx_logger.cpp \
    $(LOCAL_DIR)/global.cpp \
    $(LOCAL_DIR)/logger.cpp \

MODULE_EXPORT := so
MODULE_SO_NAME := syslog

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-logger

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/c \
    system/ulib/zircon

include make/module.mk
