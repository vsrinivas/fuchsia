# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/client.c \
    $(LOCAL_DIR)/client.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/fs \
    system/ulib/sync \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/zx \

MODULE_HEADER_DEPS := system/ulib/ddk

MODULE_PACKAGE = static

include make/module.mk
