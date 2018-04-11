# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

BOOT_SHIM_DIR := $(GET_LOCAL_DIR)

BOOT_SHIM_BUILDDIR := $(BUILDDIR)/boot-shim/$(PLATFORM_BOARD_NAME)

BOOT_SHIM_OBJ := \
    $(BOOT_SHIM_BUILDDIR)/boot-shim.S.o \
    $(BOOT_SHIM_BUILDDIR)/boot-shim.c.o \
    $(BOOT_SHIM_BUILDDIR)/debug.c.o \
    $(BOOT_SHIM_BUILDDIR)/devicetree.c.o \
    $(BOOT_SHIM_BUILDDIR)/util.c.o \

BOOT_SHIM_LD := $(BOOT_SHIM_DIR)/boot-shim.ld
BOOT_SHIM_ELF := $(BOOT_SHIM_BUILDDIR)/boot-shim.elf
BOOT_SHIM_BIN := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-boot-shim.bin

KERNEL_ALIGN := 65536
SHIM_DEFINES := -DKERNEL_ALIGN=$(KERNEL_ALIGN)
SHIM_INCLUDES := -Ikernel/include -Ikernel/arch/arm64/include -Isystem/public
SHIM_INCLUDES += -Isystem/ulib/ddk/include  # for ddk/protocol/platform-defs.h
SHIM_CFLAGS := $(NO_SAFESTACK) $(NO_SANITIZERS)

# for including target specific headers
SHIM_INCLUDES += -Ikernel/target/arm64/board/$(PLATFORM_BOARD_NAME)

# capture board specific variables for the build rules
$(BOOT_SHIM_BIN): BOOT_SHIM_BUILDDIR:=$(BOOT_SHIM_BUILDDIR)
$(BOOT_SHIM_BIN): BOOT_SHIM_OBJ:=$(BOOT_SHIM_OBJ)
$(BOOT_SHIM_BIN): BOOT_SHIM_ELF:=$(BOOT_SHIM_ELF)
$(BOOT_SHIM_BIN): BOOT_SHIM_BIN:=$(BOOT_SHIM_BIN)
$(BOOT_SHIM_BIN): SHIM_INCLUDES:=$(SHIM_INCLUDES)

$(BOOT_SHIM_BUILDDIR)/%.S.o: $(BOOT_SHIM_DIR)/%.S
	@$(MKDIR)
	$(call BUILDECHO, compiling $<)
	$(NOECHO)$(CC) $(SHIM_INCLUDES) $(SHIM_DEFINES) $(GLOBAL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) $(GLOBAL_OPTFLAGS) $(GLOBAL_ASMFLAGS) $(KERNEL_ASMFLAGS) $(ARCH_ASMFLAGS) -c $< -MMD -MP -MT $@ -MF $(@:%o=%d) -o $@

$(BOOT_SHIM_BUILDDIR)/%.c.o: $(BOOT_SHIM_DIR)/%.c
	@$(MKDIR)
	$(call BUILDECHO, compiling $<)
	$(NOECHO)$(CC) $(SHIM_INCLUDES) $(SHIM_DEFINES) $(GLOBAL_COMPILEFLAGS) $(KERNEL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) $(GLOBAL_OPTFLAGS) $(GLOBAL_CFLAGS) $(KERNEL_CFLAGS) $(ARCH_CFLAGS) $(SHIM_CFLAGS) -c $< -MMD -MP -MT $@ -MF $(@:%o=%d) -o $@

$(BOOT_SHIM_ELF): $(BOOT_SHIM_OBJ) $(BOOT_SHIM_LD)
	$(call BUILDECHO,linking $@)
	$(NOECHO)$(LD) $(GLOBAL_LDFLAGS) $(KERNEL_LDFLAGS) --build-id=none $(BOOT_SHIM_OBJ) -T $(BOOT_SHIM_LD) -o $@

$(BOOT_SHIM_BIN): $(BOOT_SHIM_ELF)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(OBJCOPY) -O binary $< $@

BOOT_SHIM_DEPS := $(patsubst %.o,%.d,$(BOOT_SHIM_OBJ))
-include $(BOOT_SHIM_DEPS)

GENERATED += $(BOOT_SHIM_BIN)
