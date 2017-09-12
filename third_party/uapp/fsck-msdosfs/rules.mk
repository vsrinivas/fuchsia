# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

MODULE_SRCS += \
    $(LOCAL_DIR)/boot.c \
    $(LOCAL_DIR)/check.c \
    $(LOCAL_DIR)/dir.c \
    $(LOCAL_DIR)/fat.c \
    $(LOCAL_DIR)/main.c \

MODULE_NAME := fsck-msdosfs

MODULE_LIBS := system/ulib/fdio system/ulib/zircon system/ulib/c

include make/module.mk
