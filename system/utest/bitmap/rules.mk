# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/raw-bitmap-tests.cpp \
    $(LOCAL_DIR)/rle-bitmap-tests.cpp \

MODULE_NAME := bitmap-test

MODULE_LIBS := \
    ulib/musl \
    ulib/mxcpp \
    ulib/mxio \
    ulib/mxtl \
    ulib/bitmap \
    ulib/unittest \

include make/module.mk
