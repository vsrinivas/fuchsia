# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

ULIB_DIR := third_party/ulib/uboringssl
SRC_DIR := $(ULIB_DIR)/crypto

KERNEL_INCLUDES += $(ULIB_DIR)/include

MODULE_SRCS := \
    $(SRC_DIR)/chacha/chacha.c \
    $(SRC_DIR)/fipsmodule/sha/sha256.c \
    $(SRC_DIR)/fipsmodule/sha/sha512.c \

include make/module.mk
