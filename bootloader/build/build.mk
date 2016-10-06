# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ALL	:=
APPS	:=
DEPS	:=

QUIET	?= @

EFI_SECTIONS	:= .text .data .reloc
EFI_SECTIONS	:= $(patsubst %,-j %,$(EFI_SECTIONS))

ifneq ($(VERBOSE),)
$(info CFLAGS   := $(EFI_CFLAGS))
$(info LDFLAGS  := $(EFI_LDFLAGS))
$(info SECTIONS := $(EFI_SECTIONS))
endif

out/%.o: %.c
	@mkdir -p $(dir $@)
	@echo compiling: $@
	$(QUIET)$(EFI_CC) -MMD -MP -o $@ -c $(EFI_CFLAGS) $<

out/%.efi: out/%.so
	@mkdir -p $(dir $@)
	@echo building: $@
	$(QUIET)$(EFI_OBJCOPY) --target=pei-$(subst _,-,$(ARCH)) --subsystem 10 $(EFI_SECTIONS) $< $@
	$(QUIET)if [ "`$(EFI_NM) $< | grep ' U '`" != "" ]; then echo "error: $<: undefined symbols"; $(EFI_NM) $< | grep ' U '; rm $<; exit 1; fi

# _efi_app <basename> <obj-files> <dep-files>
define _efi_app
ALL	+= out/$1.efi
APPS	+= out/$1.efi
DEPS	+= $3
out/$1.so: $2 $(EFI_CRT0) out/libxefi.a
	@mkdir -p $$(dir $$@)
	@echo linking: $$@
	$(QUIET)$(EFI_LD) -o $$@ $(EFI_LDFLAGS) $2 $(EFI_LIBS)
	$(QUIET)if ! $(EFI_READELF) -r $$@ | grep -q 'no relocations'; then \
	    echo "error: $$@ has relocations"; \
	    $(EFI_READELF) -r $$@; \
	    rm $$@; \
	    exit 1;\
	fi
endef

efi_app = $(eval $(call _efi_app,$(strip $1),\
$(patsubst %.c,out/%.o,$2),\
$(patsubst %.c,out/%.d,$2)))
