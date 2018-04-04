# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS := \
    $(LOCAL_DIR)/directory.cpp \
    $(LOCAL_DIR)/dnode.cpp \
    $(LOCAL_DIR)/file.cpp \
    $(LOCAL_DIR)/memfs.cpp \
    $(LOCAL_DIR)/vmo.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fs \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async-loop.cpp \
    system/ulib/async-loop \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/sync \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon \

MODULE_PACKAGE := static

include make/module.mk
