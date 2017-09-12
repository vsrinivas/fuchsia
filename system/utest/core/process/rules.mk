# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/process.cpp

MODULE_NAME := process-test

MODULE_LIBS := \
    system/ulib/unittest system/ulib/fdio system/ulib/zircon system/ulib/c system/ulib/mini-process

include make/module.mk
