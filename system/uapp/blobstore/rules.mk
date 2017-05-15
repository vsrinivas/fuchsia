# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

MODULE_NAME := blobstore

# app main
MODULE_SRCS := \
    $(LOCAL_DIR)/blobstore.cpp \
    $(LOCAL_DIR)/blobstore-ops.cpp \
    $(LOCAL_DIR)/main.cpp \
    $(LOCAL_DIR)/rpc.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fs \
    system/ulib/merkle \
    third_party/ulib/cryptolib \
    system/ulib/mx \
    system/ulib/mxalloc \
    system/ulib/mxcpp \
    system/ulib/mxtl \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/magenta \
    system/ulib/mxio \
    system/ulib/bitmap \

include make/module.mk
