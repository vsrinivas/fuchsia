# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := misc

MODULE_SRCS += \
    $(LOCAL_DIR)/main.cpp

MODULE_NAME := trace-example

MODULE_STATIC_LIBS := \
    system/ulib/trace-provider \
    system/ulib/trace \
    system/ulib/async \
    system/ulib/async.loop \
    system/ulib/mxcpp \
    system/ulib/fbl \
    system/ulib/mx

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/mxio \
    system/ulib/magenta \
    system/ulib/trace-engine

include make/module.mk
