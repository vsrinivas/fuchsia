# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := ddk

MODULE_SRCS += \
    $(LOCAL_DIR)/main.c

MODULE_NAME := driver-tests

MODULE_LIBS := system/ulib/fdio system/ulib/c system/ulib/zircon system/ulib/unittest

include make/module.mk
