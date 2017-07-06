# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

DEVMGR_SRCS := system/core/devmgr

MODULE := $(LOCAL_DIR)

MODULE_NAME := driver

MODULE_TYPE := userlib

MODULE_EXPORT := so
MODULE_SO_NAME := driver

MODULE_COMPILEFLAGS := -fvisibility=hidden

MODULE_SRCS := \
	$(LOCAL_DIR)/usb.c \
	$(DEVMGR_SRCS)/devhost.c \
	$(DEVMGR_SRCS)/devhost-api.c \
	$(DEVMGR_SRCS)/devhost-core.c \
	$(DEVMGR_SRCS)/devhost-rpc-server.c \
	$(DEVMGR_SRCS)/devhost-shared.c \

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/sync system/ulib/port

MODULE_LIBS := system/ulib/mxio system/ulib/magenta system/ulib/c

include make/module.mk
