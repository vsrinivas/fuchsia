# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := system/uapp/devmgr

MODULE := system/ulib/driver

MODULE_NAME := driver

MODULE_TYPE := userlib

MODULE_DEFINES += LIBDRIVER=1

MODULE_SRCS := \
	$(LOCAL_DIR)/devmgr.c \
	$(LOCAL_DIR)/devhost.c \
	$(LOCAL_DIR)/binding.c \
	$(LOCAL_DIR)/rpc-device.c \
	$(LOCAL_DIR)/api.c \
	system/udev/kpci/kpci.c \
	system/udev/kpci/protocol.c \
	$(LOCAL_DIR)/main.c \

MODULE_HEADER_DEPS := ulib/ddk

MODULE_DEPS := ulib/musl ulib/mxio ulib/magenta

MODULE_EXPORT := driver

include make/module.mk
