# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/intel-rng.c \

MODULE_DEPS += dev/hw_rng

# This flag is required to use _rdseed64_step from <x86intrin.h>.
MODULE_COMPILEFLAGS += -mrdseed

include make/module.mk
