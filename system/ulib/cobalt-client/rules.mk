# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/counter.cpp \
    $(LOCAL_DIR)/histogram.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-cobalt \

MODULE_PACKAGE := src

include make/module.mk
