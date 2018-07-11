# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

BOOT_SHIM_DIR := $(GET_LOCAL_DIR)

BOOT_SHIM_BUILDDIR := $(BUILDDIR)/boot-shim/$(PLATFORM_BOARD_NAME)

BOOT_SHIM_SRCS := \
    $(BOOT_SHIM_DIR)/boot-shim.S \
    $(BOOT_SHIM_DIR)/boot-shim.c \
    $(BOOT_SHIM_DIR)/debug.c \
    $(BOOT_SHIM_DIR)/devicetree.c \
    $(BOOT_SHIM_DIR)/util.c \
    kernel/lib/libc/string/memset.c \
    system/ulib/libzbi/zbi.c \

BOOT_SHIM_OBJS := $(BOOT_SHIM_SRCS:%=$(BOOT_SHIM_BUILDDIR)/%.o)

ALLSRCS += $(BOOT_SHIM_SRCS)
ALLOBJS += $(BOOT_SHIM_OBJS)

BOOT_SHIM_LD := $(BOOT_SHIM_DIR)/boot-shim.ld
BOOT_SHIM_ELF := $(BOOT_SHIM_BUILDDIR)/boot-shim.elf
BOOT_SHIM_BIN := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-boot-shim.bin

KERNEL_ALIGN := 65536
SHIM_DEFINES := -DKERNEL_ALIGN=$(KERNEL_ALIGN)
SHIM_INCLUDES := -Ikernel/include -Ikernel/lib/libc/include -Isystem/public
SHIM_INCLUDES += -Ikernel/arch/arm64/include
SHIM_INCLUDES += -Isystem/ulib/ddk/include  # for ddk/protocol/platform-defs.h
SHIM_INCLUDES += -Isystem/ulib/libzbi/include
SHIM_CFLAGS := $(NO_SAFESTACK) $(NO_SANITIZERS)

# The shim code runs with alignment checking enabled, so make sure the
# compiler doesn't use any unaligned memory accesses.
SHIM_CFLAGS += -mstrict-align

# for including target specific headers
SHIM_INCLUDES += -Ikernel/target/arm64/board/$(PLATFORM_BOARD_NAME)

# capture board specific variables for the build rules
$(BOOT_SHIM_BIN): BOOT_SHIM_BUILDDIR:=$(BOOT_SHIM_BUILDDIR)
$(BOOT_SHIM_BIN): BOOT_SHIM_OBJS:=$(BOOT_SHIM_OBJS)
$(BOOT_SHIM_BIN): BOOT_SHIM_ELF:=$(BOOT_SHIM_ELF)
$(BOOT_SHIM_BIN): BOOT_SHIM_BIN:=$(BOOT_SHIM_BIN)
$(BOOT_SHIM_BIN): SHIM_INCLUDES:=$(SHIM_INCLUDES)

$(BOOT_SHIM_BUILDDIR)/%.S.o: %.S
	@$(MKDIR)
	$(call BUILDECHO, compiling $<)
	$(NOECHO)$(CC) $(SHIM_INCLUDES) $(SHIM_DEFINES) $(GLOBAL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) $(GLOBAL_OPTFLAGS) $(GLOBAL_ASMFLAGS) $(KERNEL_ASMFLAGS) $(ARCH_ASMFLAGS) -c $< -MMD -MP -MT $@ -MF $(@:%o=%d) -o $@

$(BOOT_SHIM_BUILDDIR)/%.c.o: %.c
	@$(MKDIR)
	$(call BUILDECHO, compiling $<)
	$(NOECHO)$(CC) $(SHIM_INCLUDES) $(SHIM_DEFINES) $(GLOBAL_COMPILEFLAGS) $(KERNEL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) $(GLOBAL_OPTFLAGS) $(GLOBAL_CFLAGS) $(KERNEL_CFLAGS) $(ARCH_CFLAGS) $(SHIM_CFLAGS) -c $< -MMD -MP -MT $@ -MF $(@:%o=%d) -o $@

$(BOOT_SHIM_ELF): $(BOOT_SHIM_OBJS) $(BOOT_SHIM_LD)
	$(call BUILDECHO,linking $@)
	$(NOECHO)$(LD) $(GLOBAL_LDFLAGS) $(KERNEL_LDFLAGS) --build-id=none $(BOOT_SHIM_OBJS) -defsym KERNEL_ALIGN=$(KERNEL_ALIGN) -T $(BOOT_SHIM_LD) -o $@

$(BOOT_SHIM_BIN): $(BOOT_SHIM_ELF)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(OBJCOPY) -O binary $< $@
GENERATED += $(BOOT_SHIM_BIN)
