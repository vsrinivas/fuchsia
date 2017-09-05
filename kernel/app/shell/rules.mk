# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

GLOBAL_DEFINES += _MX_KERNEL_HAS_SHELL=1

MODULE_DEPS += \
	kernel/lib/console

MODULE_SRCS += \
	$(LOCAL_DIR)/shell.cpp

include make/module.mk
