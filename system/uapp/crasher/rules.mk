# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := misc

MODULE_SRCS += \
    $(LOCAL_DIR)/crasher.c \
    $(LOCAL_DIR)/cpp_specific.cpp \

MODULE_NAME := crasher

MODULE_LIBS := system/ulib/mxio system/ulib/c system/ulib/magenta
MODULE_STATIC_LIBS := system/ulib/mxcpp

MODULE_COMPILEFLAGS := -fstack-protector-all

include make/module.mk
