LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

KERNEL_INCLUDES += $(LOCAL_DIR)/source/include

MODULE_SRCS += $(LOCAL_DIR)/safe_numerics_unittest.cpp
MODULE_DEPS += lib/unittest
MODULE_DEPS += lib/mxtl

include make/module.mk
