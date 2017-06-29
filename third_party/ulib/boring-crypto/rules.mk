# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
SRC_DIR := $(LOCAL_DIR)/crypto

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(SRC_DIR)/chacha/chacha.c \

MODULE_NAME := boring-crypto

MODULE_LIBS := \
    system/ulib/mxio \
    system/ulib/c \

include make/module.mk
