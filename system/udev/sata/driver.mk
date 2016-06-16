LOCAL_DIR := $(GET_LOCAL_DIR)

DRIVER_SRCS += \
    $(LOCAL_DIR)/ahci.c \
    $(LOCAL_DIR)/sata.c \
    $(LOCAL_DIR)/gpt.c \
