# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/i2c.c \

MODULE_STATIC_LIBS := ulib/ddk ulib/bcm

MODULE_LIBS := ulib/driver ulib/musl ulib/magenta ulib/mxio

include make/module.mk
