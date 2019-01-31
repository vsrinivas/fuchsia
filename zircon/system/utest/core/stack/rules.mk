# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_USERTEST_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/stack-test.c \

MODULE_NAME := stack-test

MODULE_LIBS := \
    system/ulib/unittest system/ulib/fdio system/ulib/zircon system/ulib/c

MODULE_HEADER_DEPS := system/ulib/runtime

ifeq ($(call TOBOOL,$(USE_CLANG)):$(call TOBOOL,$(USE_ASAN)),true:false)
MODULE_COMPILEFLAGS += -fsanitize=safe-stack -fstack-protector-all
endif

include make/module.mk
