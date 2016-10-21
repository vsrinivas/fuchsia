LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_CFLAGS += -I$(LOCAL_DIR)/include/bcm28xx

include make/module.mk