# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/ctor.cpp

MODULE_NAME := ctor-test

MODULE_STATIC_LIBS := system/ulib/mxcpp
MODULE_LIBS := system/ulib/unittest system/ulib/mxio system/ulib/c
MODULE_LIBS += system/utest/ctor/dso-ctor

include make/module.mk
