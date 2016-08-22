# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/tls.cpp \

MODULE_NAME := elf-tls-test

MODULE_LIBS := \
    ulib/unittest ulib/mxio ulib/magenta ulib/musl

include make/module.mk
