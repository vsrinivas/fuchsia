# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := misc

MODULE_SRCS += \
    $(LOCAL_DIR)/benchmarks.cpp \
    $(LOCAL_DIR)/benchmarks_ntrace.cpp \
    $(LOCAL_DIR)/main.cpp

MODULE_NAME := trace-benchmark

MODULE_STATIC_LIBS := \
    system/ulib/trace \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/async-loop.cpp \
    system/ulib/async-loop \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/zx

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/trace-engine

include make/module.mk
