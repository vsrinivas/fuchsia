LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

SRC_DIR := third_party/ulib/safeint

KERNEL_INCLUDES += $(SRC_DIR)/include

MODULE_SRCS += $(SRC_DIR)/safe_numerics_unittest.cpp
MODULE_DEPS += kernel/lib/unittest
MODULE_DEPS += kernel/lib/fbl

include make/module.mk
