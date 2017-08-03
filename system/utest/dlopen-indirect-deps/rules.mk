# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/dlopen-indirect-deps.c

MODULE_NAME := dlopen-indirect-deps-test

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/mxio \
    system/ulib/c

include make/module.mk

include $(LOCAL_DIR)/dlopen-indirect-deps-test-module/rules.mk
