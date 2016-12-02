# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

# app main
MODULE_SRCS := \
    $(LOCAL_DIR)/main.c \

# "libfs"
MODULE_SRCS += \
    $(LOCAL_DIR)/bitmap.c \
    $(LOCAL_DIR)/bcache.c \
    $(LOCAL_DIR)/rpc.c \

# minfs implementation
MODULE_SRCS += \
    $(LOCAL_DIR)/minfs.c \
    $(LOCAL_DIR)/minfs-ops.c \
    $(LOCAL_DIR)/minfs-check.c \

MODULE_STATIC_LIBS := ulib/fs

MODULE_LIBS := ulib/magenta ulib/mxio ulib/musl

include make/module.mk
