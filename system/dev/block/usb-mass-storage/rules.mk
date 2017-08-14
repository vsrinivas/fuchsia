# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

# USB host driver

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/block.c \
    $(LOCAL_DIR)/usb-mass-storage.c \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/sync

MODULE_LIBS := system/ulib/driver system/ulib/magenta system/ulib/c

include make/module.mk

# USB function driver

MODULE := $(LOCAL_DIR).function

MODULE_TYPE := driver

MODULE_NAME := ums-function

MODULE_SRCS := \
    $(LOCAL_DIR)/ums-function.c \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/sync

MODULE_LIBS := system/ulib/driver system/ulib/magenta system/ulib/c

include make/module.mk
