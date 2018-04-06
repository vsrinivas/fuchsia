# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_NAME := fidlc

MODULE_TYPE := hostapp

MODULE_COMPILEFLAGS := -O0 -g

MODULE_SRCS := \
    $(LOCAL_DIR)/main.cpp \

MODULE_HOST_LIBS := \
    system/host/fidl \

MODULE_PACKAGE := bin

include make/module.mk
