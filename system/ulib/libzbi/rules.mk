# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

LOCAL_SRCS := \
    $(LOCAL_DIR)/zbi.c \

MODULE_SRCS += \
    $(LOCAL_SRCS) \
    $(LOCAL_DIR)/zbi-zx.cpp \

MODULE_STATIC_LIBS += \
    system/ulib/fbl \
    system/ulib/zx \

MODULE_PACKAGE = static

include make/module.mk

# Host version of the library.

MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS += $(LOCAL_SRCS)

include make/module.mk
