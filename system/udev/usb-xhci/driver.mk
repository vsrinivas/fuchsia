LOCAL_DIR := $(GET_LOCAL_DIR)

DRIVER_SRCS += \
    $(LOCAL_DIR)/usb-xhci.c \
    $(LOCAL_DIR)/xhci.c \
    $(LOCAL_DIR)/xhci-device-manager.c \
    $(LOCAL_DIR)/xhci-root-hub.c \
    $(LOCAL_DIR)/xhci-transfer.c \
    $(LOCAL_DIR)/xhci-trb.c \
    $(LOCAL_DIR)/xhci-util.c \
