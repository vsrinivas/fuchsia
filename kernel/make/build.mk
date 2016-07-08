# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# use linker garbage collection, if requested
ifeq ($(WITH_LINKER_GC),1)
GLOBAL_COMPILEFLAGS += -ffunction-sections -fdata-sections
GLOBAL_LDFLAGS += --gc-sections
endif

ifneq (,$(EXTRA_BUILDRULES))
-include $(EXTRA_BUILDRULES)
endif

# enable/disable the size output based on the ENABLE_BUILD_LISTFILES switch
ifeq ($(call TOBOOL,$(ENABLE_BUILD_LISTFILES)),true)
SIZECMD:=$(SIZE)
else
SIZECMD:=true
endif

$(OUTLKBIN): $(OUTLKELF)
	@echo generating image: $@
	$(NOECHO)$(OBJCOPY) -O binary $< $@

$(OUTLKELF).hex: $(OUTLKELF)
	@echo generating hex file: $@
	$(NOECHO)$(OBJCOPY) -O ihex $< $@

$(OUTLKELF): $(ALLMODULE_OBJS) $(EXTRA_OBJS) $(LINKER_SCRIPT)
	@echo linking $@
	$(NOECHO)$(LD) $(GLOBAL_LDFLAGS) -dT $(LINKER_SCRIPT) \
		$(ALLMODULE_OBJS) $(EXTRA_OBJS) $(LIBGCC) -o $@
	$(NOECHO)$(SIZECMD) -t --common $(sort $(ALLMODULE_OBJS)) $(EXTRA_OBJS)
	$(NOECHO)$(SIZECMD) $@

$(OUTLKELF).sym: $(OUTLKELF)
	@echo generating symbols: $@
	$(NOECHO)$(OBJDUMP) -t $< | $(CPPFILT) > $@

$(OUTLKELF).sym.sorted: $(OUTLKELF)
	@echo generating sorted symbols: $@
	$(NOECHO)$(OBJDUMP) -t $< | $(CPPFILT) | sort > $@

$(OUTLKELF).lst: $(OUTLKELF)
	@echo generating listing: $@
	$(NOECHO)$(OBJDUMP) $(OBJDUMP_LIST_FLAGS) -d $< | $(CPPFILT) > $@

$(OUTLKELF).debug.lst: $(OUTLKELF)
	@echo generating listing: $@
	$(NOECHO)$(OBJDUMP) $(OBJDUMP_LIST_FLAGS) -S $< | $(CPPFILT) > $@

$(OUTLKELF).dump: $(OUTLKELF)
	@echo generating objdump: $@
	$(NOECHO)$(OBJDUMP) -x $< > $@

$(OUTLKELF).size: $(OUTLKELF)
	@echo generating size map: $@
	$(NOECHO)$(NM) -S --size-sort $< > $@

$(OUTLKELF)-gdb.py: scripts/$(LKNAME).elf-gdb.py
	@echo generating $@
	$(NOECHO)cp -f $< $@

# print some information about the build
#$(BUILDDIR)/srcfiles.txt:
#	@echo generating $@
#	$(NOECHO)echo $(sort $(ALLSRCS)) | tr ' ' '\n' > $@
#
#.PHONY: $(BUILDDIR)/srcfiles.txt
#GENERATED += $(BUILDDIR)/srcfiles.txt
#
#$(BUILDDIR)/include-paths.txt:
#	@echo generating $@
#	$(NOECHO)echo $(subst -I,,$(sort $(GLOBAL_INCLUDES))) | tr ' ' '\n' > $@
#
#.PHONY: $(BUILDDIR)/include-paths.txt
#GENERATED += $(BUILDDIR)/include-paths.txt
#
#.PHONY: $(BUILDDIR)/user-include-paths.txt
#GENERATED += $(BUILDDIR)/user-include-paths.txt

# userspace app debug info rules

$(BUILDDIR)/%.elf.dump: $(BUILDDIR)/%.elf
	@echo generating $@
	$(NOECHO)$(OBJDUMP) -x $< > $@

$(BUILDDIR)/%.elf.lst: $(BUILDDIR)/%.elf
	@echo generating $@
	$(NOECHO)$(OBJDUMP) $(OBJDUMP_LIST_FLAGS) -d $< > $@

$(BUILDDIR)/%.elf.strip: $(BUILDDIR)/%.elf
	@echo generating $@
	$(NOECHO)$(STRIP) -d $< -o $@

# generate a new manifest and compare to see if it differs from the previous one
.PHONY: usermanifestfile
$(USER_MANIFEST): usermanifestfile
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)echo $(USER_MANIFEST_LINES) | tr ' ' '\n' | sort > $@.tmp
	$(NOECHO)$(call TESTANDREPLACEFILE,$@.tmp,$@)

GENERATED += $(USER_MANIFEST)

# build useful tools
MKBOOTFS := $(BUILDDIR)/tools/mkbootfs
BOOTSERVER := $(BUILDDIR)/tools/bootserver
LOGLISTENER := $(BUILDDIR)/tools/loglistener

$(BUILDDIR)/tools/%: system/tools/%.c
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)cc -Wall -o $@ $<

GENERATED += $(MKBOOTFS) $(BOOTSERVER) $(LOGLISTENER)

# Manifest Lines are bootfspath=buildpath
# Extract the part after the = for each line
# to generate dependencies
USER_MANIFEST_DEPS := $(foreach x,$(USER_MANIFEST_LINES),$(lastword $(subst =,$(SPACE),$(strip $(x)))))

$(USER_BOOTFS): $(MKBOOTFS) $(BOOTSERVER) $(LOGLISTENER) $(USER_MANIFEST) $(USER_MANIFEST_DEPS)
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)$(MKBOOTFS) -o $(USER_BOOTFS) $(USER_MANIFEST)

GENERATED += $(USER_BOOTFS)

# build userspace filesystem image
$(USER_FS): $(USER_BOOTFS)
	@echo generating $@
	$(NOECHO)dd if=/dev/zero of=$@ bs=1048576 count=16
	$(NOECHO)dd if=$(USER_BOOTFS) of=$@ conv=notrunc

# add the fs image to the clean list
GENERATED += $(USER_FS)


# generate linkage dependencies for userspace apps after
# all modules have been evaluated, so we can recursively
# expand the module dependencies and handle cases like
# APP A depends on LIB B which depends on LIB C
#
# Duplicates are removed from the list with $(sort), which
# works due to how we're linking binaries now.  If we shift
# to true .a files we'll need to get fancier here.
EXPAND_ADEPS = $(1) $(foreach DEP,$(MODULE_$(1)_ALIBS),$(call EXPAND_ADEPS,$(DEP)))
GET_USERAPP_ADEPS = $(sort $(foreach DEP,$(MODULE_$(1)_ALIBS),$(call EXPAND_ADEPS,$(DEP))))
GET_USERAPP_ALIBS = $(foreach DEP,$(call GET_USERAPP_ADEPS,$(1)),$(MODULE_$(DEP)_OUTNAME).mod.o)

# shared library deps are non-recursive
GET_USERAPP_SOLIBS = $(foreach DEP,$(MODULE_$(1)_SOLIBS),$(MODULE_$(DEP)_LIBNAME).so)

# Template for the link rule for a user app
define link_userapp
$(1): $(USER_LINKER_SCRIPT) $(LIBC_CRT1_OBJ) $(2) $(3) $(4)
	@$(MKDIR)
	@echo linking $$@
	$(NOECHO)$(LD) $(GLOBAL_LDFLAGS) -T $(USER_LINKER_SCRIPT) $(ARCH_LDFLAGS) \
	$(LIBC_CRT1_OBJ) $(2) $(3) $(4) $(LIBGCC) -o $$@
endef
LINK_USERAPP = $(eval $(call link_userapp,$(strip $(1)),$(strip $(2)),$(strip $(3)),$(strip $(4))))

# Template for the link rule for a user solib
define link_userlib
$(1): $(2) $(3) $(4)
	@$(MKDIR)
	@echo linking $$@ '(dynamic)'
	$(NOECHO)$(LD) $(GLOBAL_MODULE_LDFLAGS) -shared -soname $(5) $(2) $(3) $(4) -o $$@
endef
LINK_USERLIB = $(eval $(call link_userlib,$(strip $(1)),$(strip $(2)),$(strip $(3)),$(strip $(4)),$(5)))

# For each user app module, generate a link rule
$(foreach app,$(ALLUSER_MODULES),\
	$(eval $(call LINK_USERAPP,\
	$(MODULE_$(app)_OUTNAME).elf,\
	$(MODULE_$(app)_OBJS),\
	$(call GET_USERAPP_ALIBS,$(app)),\
	$(call GET_USERAPP_SOLIBS,$(app)))))

# For each user lib module, generate a link rule
$(foreach lib,$(ALLUSER_LIBS),\
	$(eval $(call LINK_USERLIB,\
	$(MODULE_$(lib)_LIBNAME).so,\
	$(MODULE_$(lib)_OBJS),\
	$(call GET_USERAPP_ALIBS,$(lib)),\
	$(call GET_USERAPP_SOLIBS,$(lib)),\
	lib$(MODULE_$(lib)_SONAME).so)))
