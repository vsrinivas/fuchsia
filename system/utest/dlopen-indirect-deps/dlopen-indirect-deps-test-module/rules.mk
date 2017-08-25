# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib
MODULE_GROUP := test

MODULE_SO_NAME := dlopen-indirect-deps-test-module

MODULE_SRCS := $(LOCAL_DIR)/test-module.c

MODULE_LIBS := $(LOCAL_DIR)/dlopen-indirect-deps-test-liba

include make/module.mk
