LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/cryptolib.c

MODULE_COMPILEFLAGS := -Wvla

include make/module.mk
