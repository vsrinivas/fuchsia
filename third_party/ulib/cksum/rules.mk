LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/adler32.c \
    $(LOCAL_DIR)/crc16.c \
    $(LOCAL_DIR)/crc32.c

MODULE_CFLAGS := -Wno-strict-prototypes

include make/module.mk
