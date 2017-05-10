# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := hostapp

MODULE_COMPILEFLAGS := -O0 -g

MODULE_SRCS := \
    $(LOCAL_DIR)/identifier_table.cpp \
    $(LOCAL_DIR)/lexer.cpp \
    $(LOCAL_DIR)/main.cpp \
    $(LOCAL_DIR)/parser.cpp \
    $(LOCAL_DIR)/source_manager.cpp \

include make/module.mk
