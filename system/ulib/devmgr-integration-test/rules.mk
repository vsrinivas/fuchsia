# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_GROUP := test

MODULE_SRCS += \
    $(LOCAL_DIR)/launcher.cpp \
    $(LOCAL_DIR)/file-wait.cpp \

MODULE_SO_NAME := devmgr-integration-test

MODULE_LIBS := \
	system/ulib/c \
	system/ulib/devmgr-launcher \
	system/ulib/fdio \
	system/ulib/zircon \

MODULE_STATIC_LIBS := \
	system/ulib/fbl \
	system/ulib/zx \
	system/ulib/zxcpp \

MODULE_PACKAGE := shared

include make/module.mk
