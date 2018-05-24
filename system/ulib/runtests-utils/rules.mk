# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

#
# Userspace library.
#

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_GROUP := test

MODULE_SRCS += \
    $(LOCAL_DIR)/log-exporter.cpp \
    $(LOCAL_DIR)/runtests-utils.cpp \
    $(LOCAL_DIR)/fuchsia-run-test.cpp \

MODULE_FIDL_LIBS := \
    system/fidl/logger

# zxcpp required for fbl to work.
MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/async.cpp \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/launchpad \
    system/ulib/zircon \

include make/module.mk
