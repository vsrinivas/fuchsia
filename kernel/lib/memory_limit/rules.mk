# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/memory_limit.cpp \

MODULE_INCLUDES += \
	$(LOCAL_DIR)/include

MODULE_DEPS += \
    kernel/lib/fbl \
    kernel/lib/pretty \

include make/module.mk
