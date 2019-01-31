LOCAL_DIR := $(GET_LOCAL_DIR)

SHARED_SRCS := \
    $(LOCAL_DIR)/lz4.c \
    $(LOCAL_DIR)/lz4frame.c \
    $(LOCAL_DIR)/lz4hc.c \
    $(LOCAL_DIR)/xxhash.c

SHARED_CFLAGS := \
    -I$(LOCAL_DIR)/include/lz4 -O3 -DXXH_NAMESPACE=LZ4_ \
    -Wno-implicit-fallthrough

SHARED_LIBS := system/ulib/c \
	system/ulib/fdio \

# userlib
MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += $(SHARED_SRCS)

MODULE_LIBS := system/ulib/c

MODULE_CFLAGS += $(SHARED_CFLAGS)

include make/module.mk

# hostlib
MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS := $(SHARED_SRCS)

MODULE_COMPILEFLAGS += $(SHARED_CFLAGS)

include make/module.mk

# fuzztest
MODULE := $(LOCAL_DIR).roundtrip-fuzztest
MODULE_TYPE := fuzztest
MODULE_LIBS := $(SHARED_LIBS)

MODULE_SRCS = $(LOCAL_DIR)/lz4-fuzztest.cpp \
        $(SHARED_SRCS)

MODULE_CFLAGS += $(SHARED_CFLAGS)

include make/module.mk

# fuzztest
MODULE := $(LOCAL_DIR).decompress-fuzztest
MODULE_TYPE := fuzztest
MODULE_LIBS := $(SHARED_LIBS)

MODULE_SRCS = $(LOCAL_DIR)/lz4-decompress-fuzztest.cpp \
        $(SHARED_SRCS)

MODULE_CFLAGS += $(SHARED_CFLAGS)

include make/module.mk
