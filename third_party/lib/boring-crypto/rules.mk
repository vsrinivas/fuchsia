# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

ULIB_DIR := third_party/ulib/boring-crypto
SRC_DIR := $(ULIB_DIR)/crypto

KERNEL_INCLUDES += $(ULIB_DIR)/include

MODULE_SRCS := \
    $(LOCAL_DIR)/chacha_unittest.cpp \
    $(LOCAL_DIR)/all_tests.cpp \

MODULE_SRCS += \
    $(SRC_DIR)/chacha/chacha.c \


include make/module.mk
