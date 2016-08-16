# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

SRC_DIR := third_party/lib/cksum

MODULE_SRCS += \
	$(SRC_DIR)/adler32.c \
	$(SRC_DIR)/crc16.c \
	$(SRC_DIR)/crc32.c \
	$(SRC_DIR)/debug.c

MODULE_CFLAGS += -Wno-strict-prototypes

MODULE_EXPORT := cksum

MODULE_SO_NAME := cksum

include make/module.mk
