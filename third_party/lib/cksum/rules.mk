LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

SRC_DIR := third_party/ulib/cksum

KERNEL_INCLUDES += $(SRC_DIR)/include

MODULE_SRCS := \
    $(SRC_DIR)/adler32.c \
    $(SRC_DIR)/crc16.c \
    $(SRC_DIR)/crc32.c \

MODULE_CFLAGS := -Wno-strict-prototypes

include make/module.mk
