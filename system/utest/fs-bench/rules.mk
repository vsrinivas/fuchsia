# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := fs

MODULE_NAME := fs-bench-test

MODULE_SRCS := \
    $(LOCAL_DIR)/main.cpp \
    $(LOCAL_DIR)/bench-basic.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/mxcpp \
    system/ulib/fbl \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/mxio \
    system/ulib/fs-management \
    system/ulib/magenta \
    system/ulib/unittest \

include make/module.mk
