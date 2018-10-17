# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_NAME := zxs-test

MODULE_SRCS := \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/zxs-test.cpp \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-net \

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async-loop \
    system/ulib/async.cpp \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/zxs \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/unittest \
    system/ulib/fdio \
    system/ulib/c \
    system/ulib/zircon \

include make/module.mk
