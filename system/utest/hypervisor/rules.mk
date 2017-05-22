# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/guest.c \

ifeq ($(SUBARCH),x86-64)
MODULE_SRCS += \
    $(LOCAL_DIR)/decode.c \
    $(LOCAL_DIR)/page_table.c \
    $(LOCAL_DIR)/x86-64.S

MODULE_STATIC_LIBS := system/ulib/pretty
endif

MODULE_NAME := hypervisor-test

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/hypervisor \
    system/ulib/magenta \
    system/ulib/mxio \
    system/ulib/unittest \

include make/module.mk
