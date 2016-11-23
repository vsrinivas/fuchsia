LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

SRC_DIR := third_party/ulib/qrcodegen

KERNEL_INCLUDES += $(SRC_DIR)/include

MODULE_SRCS := $(SRC_DIR)/qrcode.cpp

include make/module.mk
