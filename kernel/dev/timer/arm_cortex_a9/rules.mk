# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

GLOBAL_DEFINES += \
	PLATFORM_HAS_DYNAMIC_TIMER=1

MODULE_DEPS += \
    lib/fixed_point

MODULE_SRCS += \
	$(LOCAL_DIR)/arm_cortex_a9_timer.c

include make/module.mk
