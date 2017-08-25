# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_NAME := mount

# app main
MODULE_SRCS := \
    $(LOCAL_DIR)/main.c \

MODULE_LIBS := \
    system/ulib/fs-management \
    system/ulib/magenta \
    system/ulib/mxio \
    system/ulib/c

include make/module.mk
