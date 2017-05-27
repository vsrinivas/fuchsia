# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/vdso-variant.c

MODULE_NAME := vdso-variant-test

MODULE_LIBS := \
    system/ulib/launchpad \
    system/ulib/mxio \
    system/ulib/magenta \
    system/ulib/c

include make/module.mk

include $(LOCAL_DIR)/helper/rules.mk
