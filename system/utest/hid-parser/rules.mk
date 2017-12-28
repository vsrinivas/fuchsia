# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/hid-parser-test.cpp \
    $(LOCAL_DIR)/hid-report-data.cpp

MODULE_NAME := hidparse-test

MODULE_STATIC_LIBS := \
    system/ulib/hid-parser \
    system/ulib/fbl \
    system/ulib/zxcpp

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/c

include make/module.mk
