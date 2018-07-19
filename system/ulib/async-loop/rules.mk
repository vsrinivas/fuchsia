# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
LOCAL_INC := $(LOCAL_DIR)/include/lib/async-loop

#
# libasync-loop.a: the message loop library
#

MODULE := $(LOCAL_DIR)
MODULE_NAME := async-loop

MODULE_TYPE := userlib

MODULE_SRCS = \
    $(LOCAL_DIR)/loop.c

MODULE_PACKAGE_SRCS := $(MODULE_SRCS)
MODULE_PACKAGE_INCS := $(LOCAL_INC)/loop.h

MODULE_STATIC_LIBS := \
    system/ulib/async

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/zircon

MODULE_PACKAGE := src

include make/module.mk

#
# libasync-loop-cpp.a: the message loop library
#

MODULE := $(LOCAL_DIR).cpp
MODULE_NAME := async-loop-cpp

MODULE_TYPE := userlib

MODULE_SRCS = \
    $(LOCAL_DIR)/loop_wrapper.cpp

MODULE_PACKAGE_SRCS := $(MODULE_SRCS)
MODULE_PACKAGE_INCS := $(LOCAL_INC)/cpp/loop.h

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async-loop \
    system/ulib/zx

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/zircon

MODULE_PACKAGE := src

include make/module.mk
