# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_NAME := fs-bench-test

MODULE_SRCS := \
    $(LOCAL_DIR)/main.cpp \
    $(LOCAL_DIR)/bench-basic.cpp \

MODULE_LIBS := \
    ulib/c \
    ulib/mxcpp \
    ulib/mxio \
    ulib/mxtl \
    ulib/fs-management \
    ulib/magenta \
    ulib/unittest \

include make/module.mk
