# Copyright 2017 The Fuchsia Authors
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

KERNEL_INCLUDES += kernel/dev/interrupt/arm_gic/v2/include

MODULE_SRCS += \
	$(LOCAL_DIR)/arm_gicv3.c

MODULE_DEPS += \
	kernel/dev/interrupt \
	kernel/dev/pdev \
	kernel/dev/pdev/interrupt \

include make/module.mk
