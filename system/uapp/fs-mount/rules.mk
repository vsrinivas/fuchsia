# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

MODULE_NAME := mount

# app main
MODULE_SRCS := \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/minfs.c \
    $(LOCAL_DIR)/mount.c \
    $(LOCAL_DIR)/fat.c \

MODULE_LIBS := ulib/magenta ulib/mxio ulib/musl ulib/launchpad

include make/module.mk
