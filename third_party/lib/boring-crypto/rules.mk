LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

SRC_DIR := third_party/ulib/boring-crypto

KERNEL_INCLUDES += $(SRC_DIR)/include

MODULE_SRCS := \
    $(SRC_DIR)/chacha.c \
    $(SRC_DIR)/chacha_unittest.cpp

include make/module.mk
