# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).clock

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS += \
	$(LOCAL_DIR)/clock.c \


MODULE_NAME := clock

MODULE_LIBS := system/ulib/fdio system/ulib/zircon system/ulib/c
MODULE_FIDL_LIBS := system/fidl/zircon-rtc

include make/module.mk


MODULE := $(LOCAL_DIR).clkctl

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS += \
	$(LOCAL_DIR)/clkctl.c \

MODULE_NAME := clkctl

MODULE_LIBS := system/ulib/fdio system/ulib/zircon system/ulib/c

include make/module.mk
