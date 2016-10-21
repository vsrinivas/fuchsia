LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/lz4.c \
    $(LOCAL_DIR)/lz4frame.c \
    $(LOCAL_DIR)/lz4hc.c \
    $(LOCAL_DIR)/xxhash.c

MODULE_LIBS := ulib/musl

MODULE_CFLAGS += -I$(LOCAL_DIR)/include/lz4 -O3 -DXXH_NAMESPACE=LZ4_

include make/module.mk
