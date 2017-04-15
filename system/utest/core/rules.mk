# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

MODULE_SRCS := \
    $(wildcard $(LOCAL_DIR)/*/*.c) \
    $(wildcard $(LOCAL_DIR)/*/*.cpp) \
    $(LOCAL_DIR)/posix-bits.c \
    $(LOCAL_DIR)/main.c \

MODULE_NAME := core-tests

MODULE_STATIC_LIBS := \
    system/ulib/runtime system/ulib/ddk system/ulib/unittest system/ulib/hexdump system/ulib/mini-process system/ulib/sync

# NOTE!  No other libraries can be added here!
# Anything else must be statically linked (MODULE_STATIC_LIBS).
# core-tests is meant to be run as the first real userland process
# (after the built-in userboot), which does not have access to the
# loader service needed to find additional shared libraries at runtime.
MODULE_LIBS := system/ulib/magenta system/ulib/c

MODULE_DEFINES := BUILD_COMBINED_TESTS=1

include make/module.mk

MODULES += $(patsubst %/rules.mk,%,$(wildcard $(LOCAL_DIR)/*/rules.mk))
