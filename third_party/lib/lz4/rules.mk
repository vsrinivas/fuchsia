LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

SRC_DIR := third_party/ulib/lz4

KERNEL_INCLUDES += $(SRC_DIR)/include

MODULE_SRCS += \
    $(SRC_DIR)/lz4.c \
    $(SRC_DIR)/lz4frame.c \
    $(SRC_DIR)/lz4hc.c \
    $(SRC_DIR)/xxhash.c

MODULE_COMPILEFLAGS += -I$(SRC_DIR)/include/lz4 -O3 -DXXH_NAMESPACE=LZ4_

include make/module.mk
