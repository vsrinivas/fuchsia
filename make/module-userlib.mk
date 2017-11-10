# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# check for disallowed options
ifneq ($(MODULE_DEPS),)
$(error $(MODULE) $(MODULE_TYPE) modules must use MODULE_{LIBS,STATIC_LIBS}, not MODULE_DEPS)
endif
ifneq ($(MODULE_HOST_LIBS)$(MODULE_HOST_SYSLIBS),)
$(error $(MODULE) $(MODULE_TYPE) modules must use MODULE_{LIBS,STATIC_LIBS}, not MODULE_HOST_{LIBS,SYSLIBS})
endif

# Things that are library-like but not "userlib" do not
# generate static libraries, nor do they cause shared
# libraries to be exported to the sysroot

ifeq ($(MODULE_TYPE),userlib)
# build static library
$(MODULE_LIBNAME).a: $(MODULE_OBJS) $(MODULE_EXTRA_OBJS)
	@$(MKDIR)
	$(call BUILDECHO,linking $@)
	@rm -f -- "$@"
	$(call BUILDCMD,$(AR),cr $@ $^)

# always build all libraries
EXTRA_BUILDDEPS += $(MODULE_LIBNAME).a
GENERATED += $(MODULE_LIBNAME).a
endif

# modules that declare a soname or install name desire to be shared libs as well
ifneq ($(MODULE_SO_NAME)$(MODULE_SO_INSTALL_NAME),)
MODULE_ALIBS := $(foreach lib,$(MODULE_STATIC_LIBS),$(call TOBUILDDIR,$(lib))/lib$(notdir $(lib)).a)
MODULE_SOLIBS := $(foreach lib,$(MODULE_LIBS),$(call TOBUILDDIR,$(lib))/lib$(notdir $(lib)).so.abi)

# Include this in every link.
MODULE_EXTRA_OBJS += scripts/dso_handle.ld

# Link the ASan runtime into everything compiled with ASan.
ifeq (,$(filter -fno-sanitize=all,$(MODULE_COMPILEFLAGS)))
MODULE_EXTRA_OBJS += $(ASAN_SOLIB)
endif

$(MODULE_LIBNAME).so: _OBJS := $(MODULE_OBJS) $(MODULE_EXTRA_OBJS)
$(MODULE_LIBNAME).so: _LIBS := $(MODULE_ALIBS) $(MODULE_SOLIBS)
ifneq (,$(MODULE_SO_NAME))
$(MODULE_LIBNAME).so: _SONAME_FLAGS := -soname lib$(MODULE_SO_NAME).so
endif
$(MODULE_LIBNAME).so: _LDFLAGS := $(GLOBAL_LDFLAGS) $(USERLIB_SO_LDFLAGS) $(MODULE_LDFLAGS)
$(MODULE_LIBNAME).so: $(MODULE_OBJS) $(MODULE_EXTRA_OBJS) $(MODULE_ALIBS) $(MODULE_SOLIBS)
	@$(MKDIR)
	$(call BUILDECHO,linking userlib $@)
	$(call BUILDCMD,$(USER_LD),$(_LDFLAGS) -shared $(_SONAME_FLAGS) \
                                   $(_OBJS) $(_LIBS) $(LIBGCC) -o $@)

EXTRA_IDFILES += $(MODULE_LIBNAME).so.id

# build list and debugging files if asked to
ifeq ($(ENABLE_BUILD_LISTFILES),true)
EXTRA_BUILDDEPS += $(MODULE_LIBNAME).so.lst
EXTRA_BUILDDEPS += $(MODULE_LIBNAME).so.sym
GENERATED += $(MODULE_LIBNAME).so.lst
GENERATED += $(MODULE_LIBNAME).so.sym
endif

ifeq ($(MODULE_TYPE),userlib)
# Only update the .so.abi file if it's changed, so things don't need
# to be relinked if the ABI didn't change.
$(MODULE_LIBNAME).so.abi: $(MODULE_LIBNAME).abi.stamp ;

# Link the ABI stub against the same DSOs the real library uses, so the
# stub gets DT_NEEDED entries.  These are not strictly part of the ABI.
# But at link time, the linker pays attention to them if the DSO has any
# undefined symbols.  In some situations, the presence of the undefined
# symbols actually is part of the ABI, so we can't omit them from the
# stub.  Since they're there, the linker will want to believe that some
# other DSO supplies them.  The old GNU linker actually looks for the
# named DSOs (via -rpath-link) and checks their symbols.  Gold simply
# notices if any DSO directly included in the link has a DT_NEEDED for
# another DSO that is not directly included in the link, and in that
# case doesn't complain about undefined symbols in the directly-included
# DSO.  LLD never complains about undefined symbols in a DSO included in
# the link, so if it were the only linker we would not add these
# DT_NEEDEDs at all.
$(MODULE_LIBNAME).abi.stamp: _SONAME := lib$(MODULE_SO_NAME).so
$(MODULE_LIBNAME).abi.stamp: _LIBS := $(MODULE_SOLIBS)
$(MODULE_LIBNAME).abi.stamp: $(MODULE_LIBNAME).abi.o $(MODULE_SOLIBS) \
			     $(MODULE_LIBNAME).abi.h scripts/shlib-symbols
	$(call BUILDECHO,generating ABI stub $(@:.abi.stamp=.so.abi))
	$(NOECHO)$(USER_LD) $(GLOBAL_LDFLAGS) --no-gc-sections \
		       -shared -soname $(_SONAME) -s \
		       $< $(_LIBS) -o $(@:.abi.stamp=.so.abi).new
# Sanity check that the ABI stub really matches the actual DSO.
	$(NOECHO)$(SHELLEXEC) scripts/shlib-symbols '$(NM)' $(@:.abi.stamp=.so.abi).new | \
	cmp $(<:.o=.h) -
# Move it into place only if it's changed.
	$(NOECHO)\
	if cmp -s $(@:.abi.stamp=.so.abi).new $(@:.abi.stamp=.so.abi); then \
	  rm $(@:.abi.stamp=.so.abi).new; \
	else \
	  mv -f $(@:.abi.stamp=.so.abi).new $(@:.abi.stamp=.so.abi); \
	fi
	$(NOECHO)touch $@

$(MODULE_LIBNAME).abi.h: $(MODULE_LIBNAME).so scripts/shlib-symbols
	$(NOECHO)$(SHELLEXEC) scripts/shlib-symbols -z '$(NM)' $< > $@

$(MODULE_LIBNAME).abi.o: $(MODULE_LIBNAME).abi.h scripts/dso-abi.h
	$(NOECHO)$(CC) $(GLOBAL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) \
		       $(ARCH_CFLAGS) -c -include scripts/dso-abi.h \
		       -xassembler-with-cpp $< -o $@

ALLUSER_LIBS += $(MODULE)
EXTRA_BUILDDEPS += $(MODULE_LIBNAME).so.abi
GENERATED += \
    $(MODULE_LIBNAME).so $(MODULE_LIBNAME).so.abi $(MODULE_LIBNAME).abi.stamp \
    $(MODULE_LIBNAME).abi.h $(MODULE_LIBNAME).abi.o
endif

ifeq ($(MODULE_SO_INSTALL_NAME),)
MODULE_SO_INSTALL_NAME := lib$(MODULE_SO_NAME).so
# At runtime, ASan-supporting libraries are found in lib/asan/ first.
ifeq ($(call TOBOOL,$(USE_ASAN)),true)
MODULE_SO_INSTALL_NAME := asan/$(MODULE_SO_INSTALL_NAME)
endif
MODULE_SO_INSTALL_NAME := lib/$(MODULE_SO_INSTALL_NAME)
endif
ifneq ($(MODULE_SO_INSTALL_NAME),-)
USER_MANIFEST_LINES += $(MODULE_GROUP)$(MODULE_SO_INSTALL_NAME)=$(MODULE_LIBNAME).so.strip
# These debug info files go in the bootfs image.
ifeq ($(and $(filter $(subst $(COMMA),$(SPACE),$(BOOTFS_DEBUG_MODULES)),$(MODULE)),yes),yes)
USER_MANIFEST_DEBUG_INPUTS += $(MODULE_LIBNAME).so
endif
endif
endif


# process packages exported to sdk as source
ifeq ($(filter src,$(MODULE_PACKAGE)),src)
MODULE_PKG_FILE := $(MODULE_BUILDDIR)/$(MODULE_NAME).pkg
MODULE_EXP_FILE := $(BUILDDIR)/export/$(MODULE_NAME).pkg

# grab all files from source, we'll filter out .h, .c, etc in the next steps
# so as to exclude build files, editor droppings, etc
MODULE_PKG_FILES := $(sort $(shell find $(MODULE_SRCDIR) -type f))

# split based on include/... and everything else
MODULE_PKG_INCS := $(filter %.h,$(filter $(MODULE_SRCDIR)/include/%,$(MODULE_PKG_FILES)))
MODULE_PKG_SRCS := $(filter %.c %.h %.cpp %.S,$(filter-out $(MODULE_SRCDIR)/include/%,$(MODULE_PKG_FILES)))

MODULE_PKG_INCS := $(foreach inc,$(MODULE_PKG_INCS),$(patsubst $(MODULE_SRCDIR)/include/%,%,$(inc))=SOURCE/$(inc))
MODULE_PKG_SRCS := $(foreach inc,$(MODULE_PKG_SRCS),$(patsubst $(MODULE_SRCDIR)/%,%,$(inc))=SOURCE/$(inc))

MODULE_PKG_DEPS := $(foreach dep,$(MODULE_LIBS) $(MODULE_STATIC_LIBS),$(lastword $(subst /,$(SPACE),$(dep))))

$(MODULE_PKG_FILE): _NAME := $(MODULE_NAME)
$(MODULE_PKG_FILE): _INCS := $(MODULE_PKG_INCS)
$(MODULE_PKG_FILE): _SRCS := $(MODULE_PKG_SRCS)
$(MODULE_PKG_FILE): _DEPS := $(MODULE_PKG_DEPS)
$(MODULE_PKG_FILE): $(MODULE_SRCDIR)/rules.mk make/module-userlib.mk
	@$(call BUILDECHO,creating package $@ ;)\
	$(MKDIR) ;\
	echo "[package]" > $@ ;\
	echo "name=$(_NAME)" >> $@ ;\
	echo "type=lib" >> $@ ;\
	echo "arch=src" >> $@ ;\
	echo "[includes]" >> $@ ;\
	for i in $(_INCS) ; do echo $$i >> $@ ; done ;\
	echo "[src]" >> $@ ;\
	for i in $(_SRCS) ; do echo $$i >> $@ ; done ;\
	echo "[deps]" >> $@ ;\
	for i in $(_DEPS) ; do echo $$i >> $@ ; done

$(MODULE_EXP_FILE): $(MODULE_PKG_FILE)
	@$(MKDIR) ;\
	if [ -f "$@" ]; then \
		if ! cmp "$<" "$@" >/dev/null 2>&1; then \
			$(if $(BUILDECHO),echo installing $@ ;)\
			cp -f $< $@; \
		fi \
	else \
		$(if $(BUILDECHO),echo installing $@ ;)\
		cp -f $< $@; \
	fi

GENERATED += $(MODULE_EXP_FILE) $(MODULE_PKG_FILE)
ALLPKGS += $(MODULE_EXP_FILE)
endif

# if the SYSROOT build feature is enabled, we will package
# up exported libraries, their headers, etc
#
# MODULE_EXPORT may contain "a" to export the static library
# MODULE_EXPORT may contain "so" to export the shared library
ifeq ($(ENABLE_BUILD_SYSROOT),true)
ifeq ($(MODULE_TYPE),userlib)

ifneq ($(filter so,$(MODULE_EXPORT)),)
ifneq ($(MODULE_SO_NAME),)
#$(info EXPORT $(MODULE) shared)
MODULE_TEMP_NAME := $(BUILDSYSROOT)/lib/lib$(MODULE_SO_NAME).so
$(call copy-dst-src,$(MODULE_TEMP_NAME),$(MODULE_LIBNAME).so.abi)
SYSROOT_DEPS += $(MODULE_TEMP_NAME)
GENERATED += $(MODULE_TEMP_NAME)

# Install debug info for exported libraries for debuggers to find.
# These files live on the development host, not the target.
# There's no point in saving separate debug info here (at least not yet),
# we just make a copy of the unstripped file.
MODULE_TEMP_NAME := $(BUILDSYSROOT)/debug-info/lib$(MODULE_SO_NAME).so
$(call copy-dst-src,$(MODULE_TEMP_NAME),$(MODULE_LIBNAME).so)
SYSROOT_DEPS += $(MODULE_TEMP_NAME)
GENERATED += $(MODULE_TEMP_NAME)
endif
endif

ifneq ($(filter a,$(MODULE_EXPORT)),)
#$(info EXPORT $(MODULE) static)
MODULE_TEMP_NAME := $(BUILDSYSROOT)/lib/lib$(MODULE_NAME).a
$(call copy-dst-src,$(MODULE_TEMP_NAME),$(MODULE_LIBNAME).a)
SYSROOT_DEPS += $(MODULE_TEMP_NAME)
GENERATED += $(MODULE_TEMP_NAME)
endif

# only install headers for exported libraries
ifneq ($(MODULE_EXPORT),)
#$(info EXPORT $(MODULE) include)
# for now, unify all headers in one pile
# TODO: ddk, etc should be packaged separately
MODULE_INSTALL_HEADERS := $(BUILDSYSROOT)/include

# Hack to work around libc/libmusl aliasing
# TODO(swetland): a long-term fix
ifeq ($(MODULE_SRCDIR),system/ulib/c)
MODULE_SRCDIR := third_party/ulib/musl
endif

# locate headers from module source public include dir
MODULE_PUBLIC_HEADERS :=\
$(shell test -d $(MODULE_SRCDIR)/include &&\
  find $(MODULE_SRCDIR)/include -name \*\.h -o -name \*\.inc)

# translate them to the destination in sysroot
MODULE_SYSROOT_HEADERS :=\
$(patsubst $(MODULE_SRCDIR)/include/%,$(MODULE_INSTALL_HEADERS)/%,$(MODULE_PUBLIC_HEADERS))

# generate rules to copy them
$(call copy-dst-src,$(MODULE_INSTALL_HEADERS)/%.h,$(MODULE_SRCDIR)/include/%.h)
$(call copy-dst-src,$(MODULE_INSTALL_HEADERS)/%.inc,$(MODULE_SRCDIR)/include/%.inc)

SYSROOT_DEPS += $(MODULE_SYSROOT_HEADERS)
GENERATED += $(MODULE_SYSROOT_HEADERS)
endif

endif # if MODULE_TYPE == userlib
endif # if ENABLE_BUILD_SYSROOT true
