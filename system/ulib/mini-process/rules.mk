# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/mini-process.c

MODULE_SO_NAME := mini-process

MODULE_LIBS := system/ulib/magenta

MODULE_COMPILEFLAGS := $(NO_SAFESTACK)

include make/module.mk
