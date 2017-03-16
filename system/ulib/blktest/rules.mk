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
    ulib/block-client \
    ulib/sync \
    ulib/hexdump \

MODULE_LIBS := \
    ulib/c \
    ulib/magenta \
    ulib/mxcpp \
    ulib/mxio \
    ulib/mxtl \
    ulib/unittest \

include make/module.mk
