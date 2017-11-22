# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

EFI_LIBDIRS := system/ulib/tftp

EFI_CFLAGS += -nostdinc -I$(LOCAL_DIR)/include -I$(LOCAL_DIR)/src
EFI_CFLAGS += -Isystem/public -Isystem/private
EFI_CFLAGS += $(foreach LIBDIR,$(EFI_LIBDIRS),-I$(LIBDIR)/include)

ifeq ($(call TOBOOL,$(USE_CLANG)),true)
EFI_LDFLAGS := /subsystem:efi_application /entry:efi_main /libpath:out
else
EFI_LINKSCRIPT := $(LOCAL_DIR)/build/efi-x86-64.lds
EFI_LDFLAGS := -nostdlib -T $(EFI_LINKSCRIPT) -pie -Lout
endif

EFI_SECTIONS := .text .data .reloc
EFI_SECTIONS := $(patsubst %,-j %,$(EFI_SECTIONS))

ifeq ($(EFI_ARCH),x86_64)
EFI_SO          := $(BUILDDIR)/bootloader/bootx64.so
EFI_BOOTLOADER  := $(BUILDDIR)/bootloader/bootx64.efi
else ifeq ($(EFI_ARCH),aarch64)
EFI_SO          := $(BUILDDIR)/bootloader/bootaa64.so
EFI_BOOTLOADER  := $(BUILDDIR)/bootloader/bootaa64.efi
endif

# Bootloader sources
EFI_SOURCES := \
    $(LOCAL_DIR)/src/osboot.c \
    $(LOCAL_DIR)/src/cmdline.c \
    $(LOCAL_DIR)/src/zircon.c \
    $(LOCAL_DIR)/src/misc.c \
    $(LOCAL_DIR)/src/netboot.c \
    $(LOCAL_DIR)/src/netifc.c \
    $(LOCAL_DIR)/src/inet6.c \
    $(LOCAL_DIR)/src/pci.c \
    $(LOCAL_DIR)/src/framebuffer.c \
    $(LOCAL_DIR)/src/device_id.c \

# libxefi sources
EFI_SOURCES += \
    $(LOCAL_DIR)/lib/efi/guids.c \
    $(LOCAL_DIR)/lib/xefi.c \
    $(LOCAL_DIR)/lib/loadfile.c \
    $(LOCAL_DIR)/lib/console-printf.c \
    $(LOCAL_DIR)/lib/ctype.c \
    $(LOCAL_DIR)/lib/printf.c \
    $(LOCAL_DIR)/lib/stdlib.c \
    $(LOCAL_DIR)/lib/string.c \
    $(LOCAL_DIR)/lib/strings.c \
    $(LOCAL_DIR)/lib/inet.c \

EFI_OBJS := $(patsubst $(LOCAL_DIR)/%.c,$(BUILDDIR)/bootloader/%.o,$(EFI_SOURCES))
EFI_DEPS := $(patsubst %.o,%.d,$(EFI_OBJS))
EFI_LIBS := $(foreach LIBDIR,$(EFI_LIBDIRS), \
                 $(BUILDDIR)/EFI_libs/lib$(notdir $(LIBDIR)).a)

$(BUILDDIR)/bootloader/%.o : $(LOCAL_DIR)/%.c
	@$(MKDIR)
	$(call BUILDECHO,compiling $<)
	$(NOECHO)$(EFI_CC) -MMD -MP -o $@ -c $(EFI_OPTFLAGS) $(EFI_COMPILEFLAGS) $(EFI_CFLAGS) $<

ifeq ($(call TOBOOL,$(USE_CLANG)),true)

$(EFI_BOOTLOADER): $(EFI_OBJS) $(EFI_LIBS)
	@$(MKDIR)
	$(call BUILDECHO,linking $@)
	$(NOECHO)$(EFI_LD) /out:$@ $(EFI_LDFLAGS) $^

else

$(EFI_SO): $(EFI_OBJS) $(EFI_LIBS)
	@$(MKDIR)
	$(call BUILDECHO,linking $@)
	$(NOECHO)$(EFI_LD) -o $@ $(EFI_LDFLAGS) $^
	$(NOECHO)if ! $(READELF) -r $@ | grep -q 'no relocations'; then \
	    echo "error: $@ has relocations"; \
	    $(READELF) -r $@; \
	    rm $@; \
	    exit 1;\
	fi

# TODO: update this to build other ARCHes
$(EFI_BOOTLOADER): $(EFI_SO)
	@$(MKDIR)
	$(call BUILDECHO,building $@)
	$(NOECHO)$(OBJCOPY) --target=pei-x86-64 --subsystem 10 $(EFI_SECTIONS) $< $@
	$(NOECHO)if [ "`$(NM) $< | grep ' U '`" != "" ]; then echo "error: $<: undefined symbols"; $(NM) $< | grep ' U '; rm $<; exit 1; fi

endif

GENERATED += $(EFI_BOOTLOADER)

.PHONY: gigaboot
gigaboot: $(EFI_BOOTLOADER)

ifeq ($(call TOBOOL,$(ENABLE_ULIB_ONLY)),false)
# x86 only by default for now
ifeq ($(ARCH),x86)
bootloader: gigaboot
all:: bootloader
endif
endif

-include $(EFI_DEPS)
