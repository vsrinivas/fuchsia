# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

MODULE_SRCS += \
    $(wildcard $(LOCAL_DIR)/*/*.c) \
    $(wildcard $(LOCAL_DIR)/*/*.cpp) \
    $(LOCAL_DIR)/main.c \

MODULE_NAME := core-tests

MODULE_STATIC_LIBS := ulib/runtime ulib/ddk ulib/unittest ulib/hexdump ulib/test-utils
MODULE_LIBS := ulib/magenta ulib/musl

MODULE_DEFINES := BUILD_COMBINED_TESTS=1

include make/module.mk

MODULES += $(patsubst %/rules.mk,%,$(wildcard $(LOCAL_DIR)/*/rules.mk))
