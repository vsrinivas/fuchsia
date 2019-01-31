# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_NAME := namespace-test

MODULE_SRCS += $(LOCAL_DIR)/namespace-test.c

MODULE_LIBS := \
    system/ulib/launchpad \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/unittest \

include make/module.mk
