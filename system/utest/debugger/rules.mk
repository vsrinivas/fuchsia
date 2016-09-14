# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_NAME := debugger-test

MODULE_SRCS += \
	$(LOCAL_DIR)/debugger.c

MODULE_STATIC_LIBS := ulib/runtime
MODULE_LIBS := \
    ulib/test-utils \
    ulib/unittest \
    ulib/launchpad \
    ulib/mxio \
    ulib/magenta \
    ulib/musl

include make/module.mk
