# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

# app main
MODULE_SRCS := \
    $(LOCAL_DIR)/main.cpp \

# "libfs"
MODULE_SRCS += \
    $(LOCAL_DIR)/bitmap.cpp \
    $(LOCAL_DIR)/bcache.cpp \
    $(LOCAL_DIR)/rpc.cpp \

# minfs implementation
MODULE_SRCS += \
    $(LOCAL_DIR)/minfs.cpp \
    $(LOCAL_DIR)/minfs-ops.cpp \
    $(LOCAL_DIR)/minfs-check.cpp \

MODULE_STATIC_LIBS := \
    ulib/fs \

MODULE_LIBS := \
    ulib/magenta \
    ulib/mxio \
    ulib/musl \
    ulib/mxcpp \
    ulib/mxio \
    ulib/mxtl \

include make/module.mk
