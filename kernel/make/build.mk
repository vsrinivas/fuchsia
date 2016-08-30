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

$(OUTLKELF): $(ALLMODULE_OBJS) $(EXTRA_OBJS) $(LINKER_SCRIPT)
	@echo linking $@
	$(NOECHO)$(LD) $(GLOBAL_LDFLAGS) -dT $(LINKER_SCRIPT) \
		$(ALLMODULE_OBJS) $(EXTRA_OBJS) $(LIBGCC) -o $@
	$(NOECHO)$(SIZECMD) -t --common $(sort $(ALLMODULE_OBJS)) $(EXTRA_OBJS)
	$(NOECHO)$(SIZECMD) $@

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
#	$(NOECHO)echo $(subst -I,,$(sort $(KERNEL_INCLUDES))) | tr ' ' '\n' > $@
#
#.PHONY: $(BUILDDIR)/include-paths.txt
#GENERATED += $(BUILDDIR)/include-paths.txt
#
#.PHONY: $(BUILDDIR)/user-include-paths.txt
#GENERATED += $(BUILDDIR)/user-include-paths.txt

# debug info rules

$(BUILDDIR)/%.dump: $(BUILDDIR)/%
	@echo generating $@
	$(NOECHO)$(OBJDUMP) -x $< > $@

$(BUILDDIR)/%.lst: $(BUILDDIR)/%
	@echo generating listing: $@
	$(NOECHO)$(OBJDUMP) $(OBJDUMP_LIST_FLAGS) -Cd $< > $@

$(BUILDDIR)/%.debug.lst: $(BUILDDIR)/%
	@echo generating debug listing: $@
	$(NOECHO)$(OBJDUMP) $(OBJDUMP_LIST_FLAGS) -CS $< > $@

$(BUILDDIR)/%.strip: $(BUILDDIR)/%
	@echo generating $@
	$(NOECHO)$(STRIP) $< -o $@

$(BUILDDIR)/%.sym: $(BUILDDIR)/%
	@echo generating symbols: $@
	$(NOECHO)$(OBJDUMP) -Ct $< > $@

$(BUILDDIR)/%.sym.sorted: $(BUILDDIR)/%
	@echo generating sorted symbols: $@
	$(NOECHO)$(OBJDUMP) -Ct $< | sort > $@

$(BUILDDIR)/%.size: $(BUILDDIR)/%
	@echo generating size map: $@
	$(NOECHO)$(NM) -S --size-sort $< > $@

# generate a new manifest and compare to see if it differs from the previous one
.PHONY: usermanifestfile
$(USER_MANIFEST): usermanifestfile
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)echo $(USER_MANIFEST_LINES) | tr ' ' '\n' | sort > $@.tmp
	$(NOECHO)$(call TESTANDREPLACEFILE,$@.tmp,$@)

GENERATED += $(USER_MANIFEST)

# Manifest Lines are bootfspath=buildpath
# Extract the part after the = for each line
# to generate dependencies
USER_MANIFEST_DEPS := $(foreach x,$(USER_MANIFEST_LINES),$(lastword $(subst =,$(SPACE),$(strip $(x)))))

$(info MKBOOTFS $(MKBOOTFS))

$(USER_BOOTFS): $(MKBOOTFS) $(USER_MANIFEST) $(USER_MANIFEST_DEPS)
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

# There is a wee bit of machine-dependence in the rodso linker script.
# So we use cpp macros/conditionals for that and generate the actual
# linker script by preprocessing the source file.
$(BUILDDIR)/rodso.ld: scripts/tmpl-rodso.ld $(GLOBAL_CONFIG_HEADER)
	@echo generating $@
	$(NOECHO)$(CC) $(GLOBAL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) \
		       -E -P -xassembler-with-cpp $< -o $@

# Each DSO target depends on this to ensure that rodso.ld is up to date and
# that the DSO gets relinked when it changes.  But rodso.ld should not go
# into the link line as an input; it needs to be specified with -T in
# MODULE_LDFLAGS instead.  So instead of having a DSO target depend on
# rodso.ld directly, we generate rodso-stamp as the thing that can be a
# dependency that harmlessly goes into the link.
$(BUILDDIR)/rodso-stamp: $(BUILDDIR)/rodso.ld
	$(NOECHO)echo '/* empty linker script */' > $@

GENERATED += $(BUILDDIR)/rodso.ld $(BUILDDIR)/rodso-stamp

# If we're using prebuilt toolchains, check to make sure
# they are up to date and complain if they are not
ifneq ($(wildcard $(LKMAKEROOT)/prebuilt/config.mk),)
# Complain if we haven't run a new enough download script to have
# the information we need to do the verification
# TODO: remove at some point in the future
ifeq ($(PREBUILT_TOOLCHAINS),)
$(info WARNING:)
$(info WARNING: prebuilt/config.mk is out of date)
$(info WARNING: run ./scripts/download-toolchain)
$(info WARNING:)
else
# For each prebuilt toolchain, check if the shafile (checked in)
# differs from the stamp file (written after downlad), indicating
# an out of date toolchain
PREBUILT_STALE :=
$(foreach tool,$(PREBUILT_TOOLCHAINS),\
$(eval A := $(shell cat $(PREBUILT_$(tool)_TOOLCHAIN_SHAFILE)))\
$(eval B := $(shell cat $(PREBUILT_$(tool)_TOOLCHAIN_STAMP)))\
$(if $(filter-out $(A),$(B)),$(eval PREBUILT_STALE += $(tool))))
ifneq ($(PREBUILT_STALE),)
# If there are out of date toolchains, complain:
$(info WARNING:)
$(foreach tool,$(PREBUILT_STALE),\
$(info WARNING: toolchain $(tool) is out of date))
$(info WARNING: run ./scripts/download-toolchain)
$(info WARNING:)
endif
endif
endif
