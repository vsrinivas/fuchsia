# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

MODULE_SRCS += \
    $(LOCAL_DIR)/guest.c \
    $(LOCAL_DIR)/vcpu.c \

MODULE_NAME := guest

MODULE_LIBS := \
	system/ulib/c \
	system/ulib/hypervisor \
	system/ulib/magenta \
	system/ulib/mxio \

include make/module.mk
