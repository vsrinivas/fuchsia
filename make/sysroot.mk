# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# rules for generating the sysroot/ dir and contents in the builddir

# identify global headers to copy to the sysroot
GLOBAL_HEADERS := $(shell find system/public -name \*\.h -o -name \*\.inc -o -name \*\.modulemap)
SYSROOT_HEADERS := $(patsubst system/public/%,$(BUILDSYSROOT)/include/%,$(GLOBAL_HEADERS))

# generate rule to copy them
$(call copy-dst-src,$(BUILDSYSROOT)/include/%.h,system/public/%.h)
$(call copy-dst-src,$(BUILDSYSROOT)/include/%.inc,system/public/%.inc)
$(call copy-dst-src,$(BUILDSYSROOT)/include/%.modulemap,system/public/%.modulemap)

SYSROOT_DEPS += $(SYSROOT_HEADERS)

# copy crt*.o files to the sysroot
SYSROOT_SCRT1 := $(BUILDSYSROOT)/lib/Scrt1.o
$(call copy-dst-src,$(SYSROOT_SCRT1),$(USER_SCRT1_OBJ))
SYSROOT_DEPS += $(SYSROOT_SCRT1)

# generate empty compatibility libs
$(BUILDSYSROOT)/lib/libm.so: third_party/ulib/musl/lib.ld
	@$(MKDIR)
	$(NOECHO)cp $< $@
$(BUILDSYSROOT)/lib/libdl.so: third_party/ulib/musl/lib.ld
	@$(MKDIR)
	$(NOECHO)cp $< $@
$(BUILDSYSROOT)/lib/libpthread.so: third_party/ulib/musl/lib.ld
	@$(MKDIR)
	$(NOECHO)cp $< $@
$(BUILDSYSROOT)/lib/librt.so: third_party/ulib/musl/lib.ld
	@$(MKDIR)
	$(NOECHO)cp $< $@

SYSROOT_DEPS += $(BUILDSYSROOT)/lib/libm.so $(BUILDSYSROOT)/lib/libdl.so $(BUILDSYSROOT)/lib/libpthread.so

# GDB specifically looks for ld.so.1, so we create that as a symlink.
$(BUILDSYSROOT)/debug-info/$(USER_SHARED_INTERP): FORCE
	@$(MKDIR)
	$(NOECHO)rm -f $@
	$(NOECHO)ln -s libc.so $@

SYSROOT_DEPS += $(BUILDSYSROOT)/debug-info/$(USER_SHARED_INTERP)

# Stable (i.e. sorted) list of the actual build inputs in the sysroot.
# (The debug-info files don't really belong in the sysroot.)
SYSROOT_LIST := \
    $(sort $(filter-out debug-info/%,$(SYSROOT_DEPS:$(BUILDSYSROOT)/%=%)))

# Generate a file containing $(SYSROOT_LIST) (but newline-separated), for
# other scripts and whatnot to consume.  Touch that file only when its
# contents change, so the whatnot can lazily trigger on changes.
$(BUILDDIR)/sysroot.list: $(BUILDDIR)/sysroot.list.stamp ;
$(BUILDDIR)/sysroot.list.stamp: FORCE
	$(NOECHO)for f in $(SYSROOT_LIST); do echo $$f; done > $(@:.stamp=.new)
	$(NOECHO)\
	if cmp -s $(@:.stamp=.new) $(@:.stamp=); then \
		rm $(@:.stamp=.new); \
	else \
		$(if $(filter false,$(call TOBOOL,$(QUIET))), \
			echo generating $(@:.stamp=);) \
		mv $(@:.stamp=.new) $(@:.stamp=); \
	fi
	$(NOECHO)touch $@

GENERATED += $(BUILDDIR)/sysroot.list $(BUILDDIR)/sysroot.list.stamp
GENERATED += $(SYSROOT_DEPS)

# add phony top level rule
.PHONY: sysroot
sysroot: $(SYSROOT_DEPS) $(BUILDDIR)/sysroot.list.stamp

# conditionally run the sysroot rule if set in the environment
ifeq ($(call TOBOOL,$(ENABLE_BUILD_SYSROOT)),true)
EXTRA_BUILDDEPS += sysroot
endif
