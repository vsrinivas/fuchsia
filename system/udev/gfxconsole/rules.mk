# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/vc-device.c \
    $(LOCAL_DIR)/vc-gfx.c \
    $(LOCAL_DIR)/textcon.c \
    $(LOCAL_DIR)/main.c \

MODULE_STATIC_LIBS := ulib/ddk ulib/gfx ulib/hid

MODULE_LIBS := ulib/driver ulib/mxio ulib/magenta ulib/musl

include make/module.mk
