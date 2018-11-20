# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/builder.cpp \
    $(LOCAL_DIR)/decoding.cpp \
    $(LOCAL_DIR)/encoding.cpp \
    $(LOCAL_DIR)/epitaph.c \
    $(LOCAL_DIR)/formatting.cpp \
    $(LOCAL_DIR)/message_buffer.cpp \
    $(LOCAL_DIR)/message_builder.cpp \
    $(LOCAL_DIR)/message.cpp \
    $(LOCAL_DIR)/transport.cpp \
    $(LOCAL_DIR)/validating.cpp \
    $(LOCAL_DIR)/handle_closing.cpp \

MODULE_LIBS := \
    system/ulib/zircon \

MODULE_COMPILEFLAGS += \
    -Isystem/ulib/fit/include \

MODULE_PACKAGE := src

include make/module.mk
