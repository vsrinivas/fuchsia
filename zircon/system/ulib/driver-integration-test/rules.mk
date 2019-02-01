# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_GROUP := test

MODULE_SRCS += \
    $(LOCAL_DIR)/launcher.cpp \

MODULE_SO_NAME := driver-integration-test

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/devmgr-integration-test \
    system/ulib/devmgr-launcher \
    system/ulib/fdio \
    system/ulib/zircon \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/fbl \
    system/ulib/libzbi \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_PACKAGE := shared
MODULE_EXPORT := so
MODULE_SO_NAME := driver-integration-test

include make/module.mk

TEST_DIR := $(LOCAL_DIR)/test

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(TEST_DIR)/main.cpp

MODULE_NAME := driver-integration-test

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/devmgr-integration-test \
    system/ulib/devmgr-launcher \
    system/ulib/driver-integration-test \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk
