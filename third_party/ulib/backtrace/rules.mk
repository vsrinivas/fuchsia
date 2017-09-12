LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_EXPORT := so

MODULE_CFLAGS += -I$(LOCAL_DIR)/include/backtrace

MODULE_SRCS := \
    $(LOCAL_DIR)/alloc.c \
    $(LOCAL_DIR)/dwarf.c \
    $(LOCAL_DIR)/elf.c \
    $(LOCAL_DIR)/fileline.c \
    $(LOCAL_DIR)/posix.c \
    $(LOCAL_DIR)/read.c \
    $(LOCAL_DIR)/sort.c \
    $(LOCAL_DIR)/state.c \

MODULE_SO_NAME := backtrace

MODULE_LIBS := \
    third_party/ulib/ngunwind system/ulib/zircon system/ulib/c

# Compile this with frame pointers so that if we crash the crashlogger
# the simplistic unwinder will work.
MODULE_COMPILEFLAGS += $(KEEP_FRAME_POINTER_COMPILEFLAGS)

include make/module.mk
