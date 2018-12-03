# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/pager.cpp \
    $(LOCAL_DIR)/userpager.cpp \
    $(LOCAL_DIR)/test_thread.cpp \

MODULE_NAME := pager-test

MODULE_STATIC_LIBS := \
    system/ulib/elf-search \
    system/ulib/fbl \
    system/ulib/inspector \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    third_party/ulib/backtrace \
    third_party/ulib/ngunwind \
    system/ulib/c \
    system/ulib/zircon \
    system/ulib/fdio \
    system/ulib/unittest

include make/module.mk
