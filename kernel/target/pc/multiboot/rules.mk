# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MULTIBOOT_BIN := $(BUILDDIR)/multiboot.bin
MULTIBOOT_ELF := $(BUILDDIR)/multiboot.elf

MULTIBOOT_LDFLAGS := -m elf_i386
MULTIBOOT_LDSCRIPT := $(LOCAL_DIR)/multiboot.ld
MULTIBOOT_COMPILEFLAGS := \
    $(NO_SAFESTACK) $(NO_SANITIZERS) \
    -m32 -mregparm=3 -fno-pic \
    -Ikernel/arch/x86/page_tables/include \
    -Ikernel/platform/pc/include \
    -Isystem/ulib/zbi/include
MULTIBOOT_SRCDEPS := $(KERNEL_CONFIG_HEADER)

MULTIBOOT_SRCS := \
    $(LOCAL_DIR)/multiboot-start.S \
    $(LOCAL_DIR)/multiboot-main.c \
    $(LOCAL_DIR)/paging.c \
    $(LOCAL_DIR)/trampoline.c \
    $(LOCAL_DIR)/util.c \
    system/ulib/libzbi/zbi.c

MULTIBOOT_OBJS := $(MULTIBOOT_SRCS:%=$(BUILDDIR)/$(LOCAL_DIR)/%.o)

$(filter %.S.o,$(MULTIBOOT_OBJS)): \
    $(BUILDDIR)/$(LOCAL_DIR)/%.S.o: %.S $(MULTIBOOT_SRCDEPS)
	@$(MKDIR)
	$(call BUILDECHO, assembling $<)
	$(NOECHO)$(CC) $(GLOBAL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) \
		       $(GLOBAL_OPTFLAGS) $(GLOBAL_ASMFLAGS) \
		       $(KERNEL_ASMFLAGS) $(ARCH_ASMFLAGS) \
		       $(KERNEL_COMPILEFLAGS) $(MULTIBOOT_COMPILEFLAGS) \
		       $(GLOBAL_INCLUDES) $(KERNEL_INCLUDES) \
		       -c $< -MMD -MP -MT $@ -MF $(@:%o=%d) -o $@

$(filter %.c.o,$(MULTIBOOT_OBJS)): \
    $(BUILDDIR)/$(LOCAL_DIR)/%.c.o: %.c $(MULTIBOOT_SRCDEPS)
	@$(MKDIR)
	$(call BUILDECHO, compiling $<)
	$(NOECHO)$(CC) $(GLOBAL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) \
		       $(GLOBAL_OPTFLAGS) $(GLOBAL_CFLAGS) \
		       $(KERNEL_CFLAGS) $(ARCH_CFLAGS) \
		       $(KERNEL_COMPILEFLAGS) $(MULTIBOOT_COMPILEFLAGS) \
		       $(GLOBAL_INCLUDES) $(KERNEL_INCLUDES) \
		       -c $< -MMD -MP -MT $@ -MF $(@:%o=%d) -o $@

ALLSRCS += $(MULTIBOOT_SRCS)
ALLOBJS += $(MULTIBOOT_OBJS)

$(MULTIBOOT_ELF): $(MULTIBOOT_LDSCRIPT) $(MULTIBOOT_OBJS)
	$(call BUILDECHO,linking $@)
	$(NOECHO)$(LD) $(GLOBAL_LDFLAGS) $(KERNEL_LDFLAGS) \
		       $(MULTIBOOT_LDFLAGS) -o $@ -T $^
GENERATED += $(MULTIBOOT_ELF)
EXTRA_IDFILES += $(MULTIBOOT_ELF).id

# We could make a Multiboot image meant to be loaded without ELF headers
# and do `objcopy -O binary` here.  But there's no reason to, and having an
# ELF binary to look at is nicer.  To remove the ELF headers instead, the
# linker script would need to remove `+ SIZEOF_HEADERS` and then the
# multiboot header would be first thing in the raw binary.
$(MULTIBOOT_BIN): $(MULTIBOOT_ELF).strip
	$(call BUILDECHO, generating $@)
	$(NOECHO)ln -f $< $@
GENERATED += $(MULTIBOOT_BIN) $(MULTIBOOT_ELF).strip

# Build the multiboot trampoline whenever building the kernel.
kernel: $(MULTIBOOT_BIN)
