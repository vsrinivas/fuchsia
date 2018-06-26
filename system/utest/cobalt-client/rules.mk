# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)
MODULE_NAME := cobalt_client_test

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/counter_test.cpp \
    $(LOCAL_DIR)/histogram_test.cpp \
    $(LOCAL_DIR)/observation_test.cpp \
    $(LOCAL_DIR)/test_main.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/cobalt-client \
    system/ulib/fbl \
    system/ulib/sync \
    system/ulib/fidl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-cobalt \

MODULE_PACKAGE := src

include make/module.mk
