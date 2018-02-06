# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := hostlib

MODULE_COMPILEFLAGS := -O0 -g

MODULE_SRCS := \
    $(LOCAL_DIR)/lib/c_generator.cpp \
    $(LOCAL_DIR)/lib/error_reporter.cpp \
    $(LOCAL_DIR)/lib/identifier_table.cpp \
    $(LOCAL_DIR)/lib/json_generator.cpp \
    $(LOCAL_DIR)/lib/lexer.cpp \
    $(LOCAL_DIR)/lib/library.cpp \
    $(LOCAL_DIR)/lib/parser.cpp \
    $(LOCAL_DIR)/lib/source_file.cpp \
    $(LOCAL_DIR)/lib/source_location.cpp \
    $(LOCAL_DIR)/lib/source_manager.cpp \

include make/module.mk
