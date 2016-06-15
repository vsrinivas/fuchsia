LOCAL_DIR := $(GET_LOCAL_DIR)

DRIVER_SRCS += \
    $(LOCAL_DIR)/usb_bus.c \
    $(LOCAL_DIR)/usb_device.c \
    $(LOCAL_DIR)/usb_hub.c \
    $(LOCAL_DIR)/generic_hub.c
