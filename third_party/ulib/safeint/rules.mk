LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

# SRC_DIR := $(LOCAL_DIR)/include/safeint

# MODULE_SRCS += \
#     $(SRC_DIR)/safe_conversion.h \
#     $(SRC_DIR)/safe_conversion_impl.h \
#     $(SRC_DIR)/safe_math.h \
#     $(SRC_DIR)/safe_math_impl.h \

include make/module.mk
