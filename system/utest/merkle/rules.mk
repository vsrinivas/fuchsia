# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/digest.cpp \
    $(LOCAL_DIR)/tree.cpp \
    $(LOCAL_DIR)/main.c

MODULE_NAME := merkle-test

MODULE_LIBS := \
    ulib/unittest \
    ulib/merkle \
    ulib/magenta \
    ulib/c \
    ulib/mxcpp \
    ulib/mxtl \
    ulib/mxio \

MODULE_STATIC_LIBS := ulib/cryptolib

include make/module.mk
