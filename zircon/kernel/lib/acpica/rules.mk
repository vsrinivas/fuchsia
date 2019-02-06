LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

ACPICA_SRC_DIR := third_party/lib/acpica/source/
KERNEL_INCLUDES += $(ACPICA_SRC_DIR)/include

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

MODULE_COMPILEFLAGS += -I$(ACPICA_SRC_DIR)/include/acpica

MODULE_SRCS += \
    $(ACPICA_SRC_DIR)/components/hardware/hwregs.c \
    $(ACPICA_SRC_DIR)/components/hardware/hwsleep.c \
    $(ACPICA_SRC_DIR)/components/hardware/hwvalid.c \
    $(ACPICA_SRC_DIR)/components/hardware/hwxface.c \
    $(ACPICA_SRC_DIR)/components/hardware/hwxfsleep.c \
    $(ACPICA_SRC_DIR)/components/tables/tbdata.c \
    $(ACPICA_SRC_DIR)/components/tables/tbfadt.c \
    $(ACPICA_SRC_DIR)/components/tables/tbfind.c \
    $(ACPICA_SRC_DIR)/components/tables/tbinstal.c \
    $(ACPICA_SRC_DIR)/components/tables/tbprint.c \
    $(ACPICA_SRC_DIR)/components/tables/tbutils.c \
    $(ACPICA_SRC_DIR)/components/tables/tbxface.c \
    $(ACPICA_SRC_DIR)/components/tables/tbxfroot.c \
    $(ACPICA_SRC_DIR)/components/utilities/utalloc.c \
    $(ACPICA_SRC_DIR)/components/utilities/utexcep.c \
    $(ACPICA_SRC_DIR)/components/utilities/utglobal.c \
    $(ACPICA_SRC_DIR)/components/utilities/utmisc.c \
    $(ACPICA_SRC_DIR)/components/utilities/utstring.c \
    $(ACPICA_SRC_DIR)/components/utilities/utxferror.c \
    $(LOCAL_DIR)/oszircon.cpp

include make/module.mk
