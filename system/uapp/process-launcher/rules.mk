# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS := \
    $(LOCAL_DIR)/launcher_impl.cpp \
    $(LOCAL_DIR)/main.cpp

MODULE_FIDL_LIBS := \
    system/fidl/process

MODULE_STATIC_LIBS := \
    system/ulib/svc \
    system/ulib/fs \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/async-loop.cpp \
    system/ulib/async-loop \
    system/ulib/trace \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/runtime \
    system/ulib/zxcpp \
    system/ulib/zx

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/launchpad \
    system/ulib/fdio \
    system/ulib/c \
    system/ulib/trace-engine \
    system/ulib/zircon

include make/module.mk
