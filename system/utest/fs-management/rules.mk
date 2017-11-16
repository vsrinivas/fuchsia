# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := fs

MODULE_SRCS += \
    $(LOCAL_DIR)/fs-management.cpp

MODULE_NAME := fs-management-test

MODULE_LIBS := \
    system/ulib/fs-management \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \

include make/module.mk
