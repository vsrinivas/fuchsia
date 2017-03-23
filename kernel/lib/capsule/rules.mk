# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

ifeq ($(ARCH),arm64)
MODULE_SRCS += \
    $(LOCAL_DIR)/capsule-arm.c
else ifeq ($(ARCH),x86)
MODULE_SRCS += \
    $(LOCAL_DIR)/capsule-intel-rtc.c
endif

include make/module.mk
