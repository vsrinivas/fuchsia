# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/iotxn.c \

MODULE_NAME := iotxn-test

MODULE_STATIC_LIBS := ulib/ddk ulib/sync
MODULE_LIBS := ulib/unittest ulib/mxio ulib/driver ulib/magenta ulib/c

include make/module.mk
