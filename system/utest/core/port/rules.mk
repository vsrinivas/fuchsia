# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/ports.cpp \

MODULE_NAME := port-test

MODULE_LIBS := \
    system/ulib/unittest system/ulib/mxio system/ulib/magenta system/ulib/c

MODULE_STATIC_LIBS := system/ulib/fbl

include make/module.mk
