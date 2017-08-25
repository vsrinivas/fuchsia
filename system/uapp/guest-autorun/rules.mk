# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := misc

MODULE_SRCS += \
    $(LOCAL_DIR)/main.c

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/magenta \
    system/ulib/mxio \

include make/module.mk
