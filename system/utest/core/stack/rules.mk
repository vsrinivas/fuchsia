# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/stack-test.c \

MODULE_NAME := stack-test

MODULE_LIBS := \
    ulib/unittest ulib/mxio ulib/magenta ulib/c

MODULE_HEADER_DEPS := ulib/runtime

ifeq ($(call TOBOOL,$(USE_CLANG)),true)
MODULE_COMPILEFLAGS += -fsanitize=safe-stack -fstack-protector-all
endif

include make/module.mk
