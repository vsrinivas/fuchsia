# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_GROUP := test

MODULE_SRCS += \
    $(LOCAL_DIR)/dso-ctor.cpp

MODULE_STATIC_LIBS := system/ulib/mxcpp
MODULE_LIBS := system/ulib/unittest system/ulib/c

MODULE_SO_NAME := dso-ctor

include make/module.mk
