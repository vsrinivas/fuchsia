# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_NAME := inspect-test

MODULE_SRCS := \
    $(LOCAL_DIR)/heap_tests.cpp \
    $(LOCAL_DIR)/inspect_tests.cpp \
    $(LOCAL_DIR)/scanner_tests.cpp \
    $(LOCAL_DIR)/snapshot_tests.cpp \
    $(LOCAL_DIR)/state_tests.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/inspect-vmo \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio  \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk
