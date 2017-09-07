# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := sys

MODULE_SRCS += \
    $(LOCAL_DIR)/race-tests.cpp

MODULE_NAME := race-test

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/launchpad \
    system/ulib/magenta \
    system/ulib/mxio \
    system/ulib/unittest \

MODULE_STATIC_LIBS := system/ulib/fbl

include make/module.mk
