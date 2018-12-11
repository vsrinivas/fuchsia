# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := ddk

MODULE_SRCS += \
    $(LOCAL_DIR)/main.cpp

MODULE_NAME := driver-tests

MODULE_STATIC_LIBS := \
	system/ulib/fbl \
	system/ulib/zx \
	system/ulib/zxcpp \

MODULE_LIBS := \
	system/ulib/c \
	system/ulib/devmgr-integration-test \
	system/ulib/devmgr-launcher \
	system/ulib/fdio \
	system/ulib/unittest \
	system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-device-test \

include make/module.mk
