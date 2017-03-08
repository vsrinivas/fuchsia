# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# use linker garbage collection, if requested
ifeq ($(call TOBOOL,$(USE_LINKER_GC)),true)
GLOBAL_COMPILEFLAGS += -ffunction-sections -fdata-sections
GLOBAL_LDFLAGS += --gc-sections
endif

ifneq (,$(EXTRA_BUILDRULES))
-include $(EXTRA_BUILDRULES)
endif

# enable/disable the size output based on the ENABLE_BUILD_LISTFILES switch
ifeq ($(ENABLE_BUILD_LISTFILES),true)
SIZECMD:=$(SIZE)
else
SIZECMD:=true
endif

$(OUTLKBIN): $(OUTLKELF)
	@echo generating image: $@
	$(NOECHO)$(OBJCOPY) -O binary $< $@

$(OUTLKELF): $(ALLMODULE_OBJS) $(EXTRA_OBJS) $(LINKER_SCRIPT)
	@echo linking $@
	$(NOECHO)$(LD) $(GLOBAL_LDFLAGS) -T $(LINKER_SCRIPT) \
		$(ALLMODULE_OBJS) $(EXTRA_OBJS) -o $@
	$(NOECHO)$(SIZECMD) -t --common $(sort $(ALLMODULE_OBJS)) $(EXTRA_OBJS)
	$(NOECHO)$(SIZECMD) $@

$(OUTLKELF)-gdb.py: $(call SCRIPTNAME, scripts/$(LKNAME).elf-gdb.py)
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
	$(NOECHO)$(OBJDUMP) $(OBJDUMP_LIST_FLAGS) -d $< | $(CPPFILT) > $@

$(BUILDDIR)/%.debug.lst: $(BUILDDIR)/%
	@echo generating debug listing: $@
	$(NOECHO)$(OBJDUMP) $(OBJDUMP_LIST_FLAGS) -S $< | $(CPPFILT) > $@

$(BUILDDIR)/%.strip: $(BUILDDIR)/%
	@echo generating $@
	$(NOECHO)$(STRIP) $< -o $@

$(BUILDDIR)/%.debug: $(BUILDDIR)/%
	@echo generating separate debug info file $@
	$(NOECHO)$(OBJCOPY) --only-keep-debug $< $@

$(BUILDDIR)/%.sym: $(BUILDDIR)/%
	@echo generating symbols: $@
	$(NOECHO)$(OBJDUMP) -t $< | $(CPPFILT) > $@

$(BUILDDIR)/%.sym.sorted: $(BUILDDIR)/%
	@echo generating sorted symbols: $@
	$(NOECHO)$(OBJDUMP) -t $< | $(CPPFILT) | sort > $@

$(BUILDDIR)/%.size: $(BUILDDIR)/%
	@echo generating size map: $@
	$(NOECHO)$(NM) -S --size-sort $< > $@

$(BUILDDIR)/%.id: $(BUILDDIR)/%
	$(NOECHO)env READELF="$(READELF)" $(call SCRIPTNAME, scripts/get-build-id) $< > $@

ifneq ($(USER_AUTORUN),)
USER_MANIFEST_LINES += autorun=$(USER_AUTORUN)
endif

# generate a new manifest and compare to see if it differs from the previous one
# USER_MANIFEST_DEBUG_INPUTS is a dependency here as the file name to put in
# the manifest must be computed *after* the input file is produced (to get the
# build id).
.PHONY: usermanifestfile
$(USER_MANIFEST): usermanifestfile $(USER_MANIFEST_DEBUG_INPUTS)
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)echo $(USER_MANIFEST_LINES) | tr ' ' '\n' | sort > $@.tmp
	$(NOECHO)for f in $(USER_MANIFEST_DEBUG_INPUTS) ; do \
	  echo debug/$$(env READELF=$(READELF) $(call SCRIPTNAME, scripts/get-build-id) $$f).debug=$$f >> $@.tmp ; \
	done
	$(NOECHO)$(call TESTANDREPLACEFILE,$@.tmp,$@)

GENERATED += $(USER_MANIFEST)

# Manifest Lines are bootfspath=buildpath
# Extract the part after the = for each line
# to generate dependencies
USER_MANIFEST_DEPS := $(foreach x,$(USER_MANIFEST_LINES),$(lastword $(subst =,$(SPACE),$(strip $(x)))))

$(info MKBOOTFS $(MKBOOTFS))

$(USER_BOOTDATA): $(MKBOOTFS) $(USER_MANIFEST) $(USER_MANIFEST_DEPS) $(ADDITIONAL_BOOTDATA_ITEMS)
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)$(MKBOOTFS) --target=boot -c -o $(USER_BOOTDATA) $(USER_MANIFEST) $(ADDITIONAL_BOOTDATA_ITEMS)

GENERATED += $(USER_BOOTDATA)

# build userspace filesystem image
$(USER_FS): $(USER_BOOTDATA)
	@echo generating $@
	$(NOECHO)dd if=/dev/zero of=$@ bs=1048576 count=16
	$(NOECHO)dd if=$(USER_BOOTDATA) of=$@ conv=notrunc

# add the fs image to the clean list
GENERATED += $(USER_FS)

# If we're using prebuilt toolchains, check to make sure
# they are up to date and complain if they are not
ifneq ($(wildcard $(LKMAKEROOT)/prebuilt/config.mk),)
# Complain if we haven't run a new enough download script to have
# the information we need to do the verification
# TODO: remove at some point in the future
ifeq ($(PREBUILT_TOOLCHAINS),)
$(info WARNING:)
$(info WARNING: prebuilt/config.mk is out of date)
$(info WARNING: run $(call SCRIPTNAME, scripts/download-toolchain))
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
$(info WARNING: run $(call SCRIPTNAME, scripts/download-toolchain))
$(info WARNING:)
endif
endif
endif
