# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/fsck.c \
    $(LOCAL_DIR)/launch.c \
    $(LOCAL_DIR)/mkfs.c \
    $(LOCAL_DIR)/mount.c \
    $(LOCAL_DIR)/ramdisk.c \

MODULE_STATIC_LIBS := \
    system/ulib/fs \

MODULE_LIBS := \
    system/ulib/launchpad \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/fdio \

MODULE_EXPORT := so
MODULE_SO_NAME := fs-management

include make/module.mk
