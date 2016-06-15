LOCAL_DIR := $(GET_LOCAL_DIR)

DRIVER_SRCS += \
    $(LOCAL_DIR)/usb_xhci.c \
    $(LOCAL_DIR)/usb_poll.c \
    $(LOCAL_DIR)/xhci.c \
    $(LOCAL_DIR)/xhci_commands.c \
    $(LOCAL_DIR)/xhci_debug.c \
    $(LOCAL_DIR)/xhci_devconf.c \
    $(LOCAL_DIR)/xhci_events.c \
    $(LOCAL_DIR)/xhci_rh.c
