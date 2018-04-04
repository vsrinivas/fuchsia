# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS := \
    $(LOCAL_DIR)/main.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/async.cpp \
    system/ulib/block-client \
    system/ulib/fbl \
    system/ulib/fs \
    system/ulib/fzl \
    system/ulib/minfs \
    system/ulib/sync \
    system/ulib/trace \
    system/ulib/trace-provider \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/bitmap \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/trace-engine \
    system/ulib/zircon \

include make/module.mk
