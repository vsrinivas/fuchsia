LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

SRC_DIR := third_party/ulib/lz4

MODULE_SRCS += \
    $(SRC_DIR)/lz4.c \
    $(SRC_DIR)/lz4frame.c \
    $(SRC_DIR)/lz4hc.c \
    $(SRC_DIR)/xxhash.c

MODULE_COMPILEFLAGS += -O3 -DXXH_NAMESPACE=LZ4_

include make/module.mk
