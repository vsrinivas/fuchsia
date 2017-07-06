LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

KERNEL_INCLUDES += $(LOCAL_DIR)/source/include

# Disable these two warnings to prevent ACPICA from cluttering the
# build output
ifeq ($(call TOBOOL,$(USE_CLANG)),false)
MODULE_CFLAGS += -Wno-discarded-qualifiers -Wno-format-signedness
else
MODULE_CFLAGS += -Wno-incompatible-pointer-types-discards-qualifiers
endif
MODULE_CFLAGS += -fno-strict-aliasing

MODULE_COMPILEFLAGS += -I$(LOCAL_DIR)/source/include/acpica

MODULE_SRCS += \
	$(LOCAL_DIR)/source/components/tables/tbdata.c \
	$(LOCAL_DIR)/source/components/tables/tbfadt.c \
	$(LOCAL_DIR)/source/components/tables/tbfind.c \
	$(LOCAL_DIR)/source/components/tables/tbinstal.c \
	$(LOCAL_DIR)/source/components/tables/tbprint.c \
	$(LOCAL_DIR)/source/components/tables/tbutils.c \
	$(LOCAL_DIR)/source/components/tables/tbxface.c \
	$(LOCAL_DIR)/source/components/tables/tbxfroot.c \
	$(LOCAL_DIR)/source/components/utilities/utalloc.c \
	$(LOCAL_DIR)/source/components/utilities/utcache.c \
	$(LOCAL_DIR)/source/components/utilities/utexcep.c \
	$(LOCAL_DIR)/source/components/utilities/utglobal.c \
	$(LOCAL_DIR)/source/components/utilities/utlock.c \
	$(LOCAL_DIR)/source/components/utilities/utmisc.c \
	$(LOCAL_DIR)/source/components/utilities/utmutex.c \
	$(LOCAL_DIR)/source/components/utilities/utownerid.c \
	$(LOCAL_DIR)/source/components/utilities/utstate.c \
	$(LOCAL_DIR)/source/components/utilities/utstring.c \
	$(LOCAL_DIR)/source/components/utilities/utxferror.c \
	$(LOCAL_DIR)/source/os_specific/service_layers/osmagenta.cpp

include make/module.mk
