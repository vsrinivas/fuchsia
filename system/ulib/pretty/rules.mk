# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/hexdump.c \
    $(LOCAL_DIR)/sizes.c

MODULE_LIBS := \
    system/ulib/mxio \
    system/ulib/c

include make/module.mk

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/test.c

MODULE_NAME := pretty-test

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/mxio \
    system/ulib/c

MODULE_STATIC_LIBS := \
    system/ulib/pretty

include make/module.mk

MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS += \
    $(LOCAL_DIR)/hexdump.c \
    $(LOCAL_DIR)/sizes.c

include make/module.mk
