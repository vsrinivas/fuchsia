# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/digest.cpp \
    $(LOCAL_DIR)/merkle-tree.cpp \
    $(LOCAL_DIR)/main.c

MODULE_NAME := digest-test

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/digest \
    system/ulib/magenta \
    system/ulib/c \
    system/ulib/mxio \

MODULE_STATIC_LIBS := \
    third_party/ulib/cryptolib \
    system/ulib/mxcpp \
    system/ulib/fbl \

include make/module.mk
