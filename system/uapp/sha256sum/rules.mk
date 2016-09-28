# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

MODULE_SRCS += \
    $(LOCAL_DIR)/sha256sum.c

MODULE_NAME := sha256sum

MODULE_STATIC_LIBS := ulib/cryptolib

MODULE_LIBS := ulib/mxio ulib/magenta ulib/musl

include make/module.mk
