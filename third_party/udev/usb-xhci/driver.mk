LOCAL_DIR := $(GET_LOCAL_DIR)

DRIVER_SRCS += \
    $(LOCAL_DIR)/usb-xhci.c \
    $(LOCAL_DIR)/xhci.c \
    $(LOCAL_DIR)/xhci-commands.c \
    $(LOCAL_DIR)/xhci-debug.c \
    $(LOCAL_DIR)/xhci-devconf.c \
    $(LOCAL_DIR)/xhci-events.c \
    $(LOCAL_DIR)/xhci-rh.c
