# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/ramdisk.cpp \

MODULE_NAME := ramdisk-test

MODULE_STATIC_LIBS := \
    system/ulib/block-client \
    system/ulib/sync \
    system/ulib/fbl \
    system/ulib/fs \
    system/ulib/fzl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/fs-management \
    system/ulib/ramdevice-client \
    system/ulib/unittest \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-hardware-ramdisk \

include make/module.mk
