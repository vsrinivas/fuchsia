# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

#MODULE := $(LOCAL_DIR).proxy
MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_NAME := nand

MODULE_SRCS += \
    $(LOCAL_DIR)/nand.c \
    $(LOCAL_DIR)/nand_driver_test.c \

MODULE_COMPILEFLAGS := -I$(LOCAL_DIR)/tests

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/sync \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/c \
    system/ulib/zircon \

include make/module.mk
