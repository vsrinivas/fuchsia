# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/mini-process.c \

MODULE_SO_NAME := mini-process

MODULE_STATIC_LIBS := $(LOCAL_DIR).subprocess system/ulib/elfload

MODULE_LIBS := system/ulib/zircon system/ulib/c

include make/module.mk

# This is in a separate module so it can have separate compilation flags.

MODULE := $(LOCAL_DIR).subprocess

MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += $(NO_SAFESTACK) $(NO_SANITIZERS)

MODULE_SRCS += \
    $(LOCAL_DIR)/subprocess.c \

include make/module.mk
