# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/block.c \
    $(LOCAL_DIR)/server.cpp \
    $(LOCAL_DIR)/txn-group.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/trace-provider \
    system/ulib/trace \
    system/ulib/ddk \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async-loop.cpp \
    system/ulib/async-loop \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/zxcpp

MODULE_LIBS := system/ulib/c \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/async.default \
    system/ulib/fdio \
    system/ulib/trace-engine

include make/module.mk
