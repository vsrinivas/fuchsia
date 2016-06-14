LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

MODULE_SRCS += \
    $(LOCAL_DIR)/handle-info.c

MODULE_NAME := handle-info-test

MODULE_DEPS := \
    ulib/musl ulib/magenta ulib/mxio

include make/module.mk
