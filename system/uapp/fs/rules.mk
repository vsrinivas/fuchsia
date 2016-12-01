# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

MODULE_NAME := fs-tests

MODULE_SRCS := \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/wrap.c \
    $(LOCAL_DIR)/test-append.c \
    $(LOCAL_DIR)/test-maxfile.c \
    $(LOCAL_DIR)/test-rw-workers.c \
    $(LOCAL_DIR)/test-basic.c \
    $(LOCAL_DIR)/test-rename.c \
    $(LOCAL_DIR)/test-sync.c \
    $(LOCAL_DIR)/test-truncate.c \

MODULE_LDFLAGS := --wrap open --wrap unlink --wrap stat --wrap mkdir
MODULE_LDFLAGS += --wrap rename --wrap truncate

MODULE_LIBS := \
    ulib/mxio ulib/launchpad ulib/magenta ulib/musl

include make/module.mk
