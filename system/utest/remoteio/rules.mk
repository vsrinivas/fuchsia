# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# disabled for now
# uses a no longer public interface

ifeq (1,2)
LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/remoteio.c \

MODULE_NAME := remoteio-test

MODULE_DEPS := \
    ulib/mxio ulib/magenta ulib/unittest ulib/musl

include make/module.mk
endif
