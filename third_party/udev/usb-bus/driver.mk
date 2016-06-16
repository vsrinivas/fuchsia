LOCAL_DIR := $(GET_LOCAL_DIR)

DRIVER_SRCS += \
    $(LOCAL_DIR)/usb-bus.c \
    $(LOCAL_DIR)/usb-device.c \
    $(LOCAL_DIR)/usb-hub.c \
    $(LOCAL_DIR)/generic-hub.c
