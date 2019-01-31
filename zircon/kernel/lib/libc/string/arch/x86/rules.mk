# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

ASM_STRING_OPS := memcpy memset

MODULE_SRCS += \
	$(LOCAL_DIR)/memcpy.S \
	$(LOCAL_DIR)/memset.S \
	$(LOCAL_DIR)/selector.cpp \
	$(LOCAL_DIR)/tests.cpp \

MODULE_DEPS += \
	kernel/lib/code_patching \
	kernel/lib/unittest \

# filter out the C implementation
C_STRING_OPS := $(filter-out $(ASM_STRING_OPS),$(C_STRING_OPS))
