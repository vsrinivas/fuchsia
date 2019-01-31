LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

KERNEL_INCLUDES += $(LOCAL_DIR)/source/include

# Disable warnings we won't fix in third-party code.
MODULE_CFLAGS += -Wno-implicit-fallthrough
ifeq ($(call TOBOOL,$(USE_CLANG)),false)
MODULE_CFLAGS += \
    -Wno-discarded-qualifiers \
    -Wno-format-signedness
else
MODULE_CFLAGS += \
    -Wno-incompatible-pointer-types-discards-qualifiers \
    -Wno-null-pointer-arithmetic
endif
# We need to specify -fno-strict-aliasing, since ACPICA has a habit of
# violating strict aliasing rules in some of its macros.  Rewriting this
# code would increase the maintenance cost of bringing in the latest
# upstream ACPICA, so instead we mitigate the problem with a compile-time
# flag.  We take the more conservative approach of disabling
# strict-aliasing-based optimizations, rather than disabling warnings.
MODULE_CFLAGS += -fno-strict-aliasing

MODULE_COMPILEFLAGS += -I$(LOCAL_DIR)/source/include/acpica

MODULE_SRCS += \
	$(LOCAL_DIR)/source/components/hardware/hwregs.c \
	$(LOCAL_DIR)/source/components/hardware/hwsleep.c \
	$(LOCAL_DIR)/source/components/hardware/hwvalid.c \
	$(LOCAL_DIR)/source/components/hardware/hwxface.c \
	$(LOCAL_DIR)/source/components/hardware/hwxfsleep.c \
	$(LOCAL_DIR)/source/components/tables/tbdata.c \
	$(LOCAL_DIR)/source/components/tables/tbfadt.c \
	$(LOCAL_DIR)/source/components/tables/tbfind.c \
	$(LOCAL_DIR)/source/components/tables/tbinstal.c \
	$(LOCAL_DIR)/source/components/tables/tbprint.c \
	$(LOCAL_DIR)/source/components/tables/tbutils.c \
	$(LOCAL_DIR)/source/components/tables/tbxface.c \
	$(LOCAL_DIR)/source/components/tables/tbxfroot.c \
	$(LOCAL_DIR)/source/components/utilities/utalloc.c \
	$(LOCAL_DIR)/source/components/utilities/utexcep.c \
	$(LOCAL_DIR)/source/components/utilities/utglobal.c \
	$(LOCAL_DIR)/source/components/utilities/utmisc.c \
	$(LOCAL_DIR)/source/components/utilities/utstring.c \
	$(LOCAL_DIR)/source/components/utilities/utxferror.c \
	$(LOCAL_DIR)/source/os_specific/service_layers/oszircon.cpp

include make/module.mk
