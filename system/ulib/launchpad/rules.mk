# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/elf.c \
    $(LOCAL_DIR)/launch.c \
    $(LOCAL_DIR)/launchpad.c \
    $(LOCAL_DIR)/mxio.c \
    $(LOCAL_DIR)/vmo.c

MODULE_EXPORT := launchpad

MODULE_SO_NAME := launchpad
MODULE_STATIC_LIBS := ulib/elfload
MODULE_LIBS := ulib/mxio ulib/magenta ulib/musl

include make/module.mk
