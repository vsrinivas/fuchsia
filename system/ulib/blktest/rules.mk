# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/blktest.cpp \

MODULE_SO_NAME := blktest

MODULE_STATIC_LIBS := \
    system/ulib/block-client \
    system/ulib/sync \
    system/ulib/pretty \
    system/ulib/mxalloc \
    system/ulib/mxcpp \
    system/ulib/mxtl \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/magenta \
    system/ulib/mxio \
    system/ulib/unittest \

include make/module.mk
