# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM := generic-arm

MODULE := $(LOCAL_DIR)

BOOT_SHIM_DIR := $(LOCAL_DIR)/boot-shim
BOOT_SHIM_OBJ_DIR := $(BUILDDIR)/boot-shim

BOOT_SHIM_OBJ := $(BOOT_SHIM_OBJ_DIR)/boot-shim.S.o $(BOOT_SHIM_OBJ_DIR)/boot-shim.c.o $(BOOT_SHIM_OBJ_DIR)/debug.c.o
BOOT_SHIM_LD := $(BOOT_SHIM_DIR)/boot-shim.ld
BOOT_SHIM_ELF := $(BUILDDIR)/boot-shim.elf
BOOT_SHIM_BIN := $(BUILDDIR)/boot-shim.bin

KERNEL_ALIGN := 65536
SHIM_INCLUDES := -Ikernel/include -Ikernel/arch/arm64/include -Isystem/public
SHIM_DEFINES := -DKERNEL_ALIGN=$(KERNEL_ALIGN)

$(BOOT_SHIM_OBJ_DIR)/%.S.o: $(BOOT_SHIM_DIR)/%.S
	@$(MKDIR)
	$(call BUILDECHO, compiling $<)
	$(NOECHO)$(CC) $(SHIM_INCLUDES) $(SHIM_DEFINES) $(GLOBAL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) $(GLOBAL_OPTFLAGS) $(GLOBAL_ASMFLAGS) $(KERNEL_ASMFLAGS) $(ARCH_ASMFLAGS) -c $< -MD -MP -MT $@ -MF $(@:%o=%d) -o $@

$(BOOT_SHIM_OBJ_DIR)/%.c.o: $(BOOT_SHIM_DIR)/%.c
	@$(MKDIR)
	$(call BUILDECHO, compiling $<)
	$(NOECHO)$(CC) $(SHIM_INCLUDES) $(SHIM_DEFINES) $(GLOBAL_COMPILEFLAGS) $(KERNEL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) $(GLOBAL_OPTFLAGS) $(GLOBAL_CFLAGS) $(KERNEL_CFLAGS) $(ARCH_CFLAGS) -c $< -MD -MP -MT $@ -MF $(@:%o=%d) -o $@

$(BOOT_SHIM_ELF): $(BOOT_SHIM_OBJ) $(BOOT_SHIM_LD)
	$(call BUILDECHO,linking $@)
	$(NOECHO)$(LD) $(GLOBAL_LDFLAGS) $(KERNEL_LDFLAGS) --build-id=none $(BOOT_SHIM_OBJ) -T $(BOOT_SHIM_LD) -o $@

$(BOOT_SHIM_BIN): $(BOOT_SHIM_ELF)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(OBJCOPY) -O binary $< $@

GENERATED += $(BOOT_SHIM_BIN)

# include rules for our various arm64 boards
include $(LOCAL_DIR)/board/*/rules.mk
