LOCAL_DIR := $(GET_LOCAL_DIR)

SHARED_SRCS := \
    $(LOCAL_DIR)/adler32.c \
    $(LOCAL_DIR)/crc16.c \
    $(LOCAL_DIR)/crc32.c

SHARED_CFLAGS := -Wno-strict-prototypes

# userlib
MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS := $(SHARED_SRCS)

MODULE_COMPILEFLAGS += $(SHARED_CFLAGS)

include make/module.mk

# hostlib
MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS := $(SHARED_SRCS)

MODULE_COMPILEFLAGS += $(SHARED_CFLAGS)

include make/module.mk
