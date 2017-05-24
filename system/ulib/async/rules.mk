# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS = \
    $(LOCAL_DIR)/async.c \
    $(LOCAL_DIR)/async_wrapper.cpp \
    $(LOCAL_DIR)/loop.c \
    $(LOCAL_DIR)/loop_wrapper.cpp \
    $(LOCAL_DIR)/timeouts.cpp

MODULE_EXPORT := so
MODULE_SO_NAME := async

MODULE_STATIC_LIBS := \
    system/ulib/mx \
    system/ulib/mxcpp

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/magenta

include make/module.mk
