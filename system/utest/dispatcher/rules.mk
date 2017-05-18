# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_NAME := dispatch-test

MODULE_SRCS := \
    $(LOCAL_DIR)/main.cpp \
    $(LOCAL_DIR)/test-multi-dispatch.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fs \
    system/ulib/mx \
    system/ulib/mxtl \
    system/ulib/mxcpp \
    system/ulib/mxalloc \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/magenta \
    system/ulib/mxio \
    system/ulib/unittest \

include make/module.mk
