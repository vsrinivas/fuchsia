# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Main library for things that want to launch devmgrs

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/launcher.cpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/launchpad \
    system/ulib/zircon \

MODULE_STATIC_LIBS := \
    system/ulib/bootsvc-protocol \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_PACKAGE := shared
MODULE_EXPORT := so
MODULE_SO_NAME := devmgr-launcher

include make/module.mk
