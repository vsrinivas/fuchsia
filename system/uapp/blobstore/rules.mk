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
    ulib/fs \
    ulib/merkle \
    ulib/cryptolib \

MODULE_LIBS := \
    ulib/c \
    ulib/magenta \
    ulib/mxio \
    ulib/bitmap \
    ulib/mxcpp \
    ulib/mxtl \

include make/module.mk
