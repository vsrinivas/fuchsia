# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/common/hid.c \
    $(LOCAL_DIR)/common/hid-fifo.c \
    $(LOCAL_DIR)/common/usb.c \
    $(LOCAL_DIR)/protocol/input.c \
    $(LOCAL_DIR)/io-buffer.c \
    $(LOCAL_DIR)/iotxn.c \

MODULE_STATIC_LIBS := ulib/hexdump ulib/sync

MODULE_EXPORT := a

include make/module.mk
