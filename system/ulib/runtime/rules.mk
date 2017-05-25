# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS := \
    $(LOCAL_DIR)/message.c \
    $(LOCAL_DIR)/mutex.c \
    $(LOCAL_DIR)/processargs.c \
    $(LOCAL_DIR)/thread.c \

MODULE_LIBS += \
    system/ulib/magenta

# for stdint.h
MODULE_HEADER_DEPS += \
    system/ulib/c

# This code is used in early startup, where safe-stack setup is not ready yet.
MODULE_COMPILEFLAGS += $(NO_SAFESTACK)

include make/module.mk
