ifeq ($(call TOBOOL,$(ENABLE_NEW_USB)),true)

LOCAL_DIR := $(GET_LOCAL_DIR)

DRIVER_SRCS += \
    $(LOCAL_DIR)/usb-hub.c \

endif
