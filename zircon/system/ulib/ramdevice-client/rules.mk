# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/ramnand.cpp \
    $(LOCAL_DIR)/ramdisk.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/devmgr-integration-test \
    system/ulib/devmgr-launcher \
    system/ulib/ddk \
    system/ulib/fzl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-device \
    system/fidl/fuchsia-hardware-nand \
    system/fidl/fuchsia-hardware-ramdisk \

MODULE_EXPORT := so
MODULE_SO_NAME := ramdevice-client

include make/module.mk
