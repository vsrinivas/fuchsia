# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_NAME := stdio-test

MODULE_SRCS += \
	$(LOCAL_DIR)/stdio.c \
	$(LOCAL_DIR)/util.c

MODULE_STATIC_LIBS := \
    ulib/elfload \
    ulib/runtime

MODULE_LIBS := \
    ulib/unittest \
    ulib/launchpad \
    ulib/mxio \
    ulib/magenta \
    ulib/c

include make/module.mk
