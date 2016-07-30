# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT


# modules
#
# args:
# MODULE : module name (required)
# MODULE_SRCS : list of source files, local path (required)
# MODULE_DEPS : other modules that this one depends on
# MODULE_HEADER_DEPS : other headers that this one depends on, in addition to MODULE_DEPS
# MODULE_DEFINES : #defines local to this module
# MODULE_OPTFLAGS : OPTFLAGS local to this module
# MODULE_COMPILEFLAGS : COMPILEFLAGS local to this module
# MODULE_CFLAGS : CFLAGS local to this module
# MODULE_CPPFLAGS : CPPFLAGS local to this module
# MODULE_ASMFLAGS : ASMFLAGS local to this module
# MODULE_SRCDEPS : extra dependencies that all of this module's files depend on
# MODULE_EXTRA_OBJS : extra .o files that should be linked with the module
# MODULE_TYPE : "userapp" for userspace executables, "userlib" for userspace library,
#               "" for standard LK module
# MODULE_LIBS : shared libraries for a userapp or userlib to depend on
# MODULE_STATIC_LIBS : static libraries for a userapp or userlib to depend on
# MODULE_SO_NAME : linkage name for the shared library
# MODULE_SO_LIBS : shared library deps for the shared library
# MODULE_SO_STATIC_LIBS : static library deps for the shared library

# the minimum module rules.mk file is as follows:
#
# LOCAL_DIR := $(GET_LOCAL_DIR)
# MODULE := $(LOCAL_DIR)
#
# MODULE_SRCS := $(LOCAL_DIR)/at_least_one_source_file.c
#
# include make/module.mk

MODULE_SRCDIR := $(MODULE)

ifeq ($(MODULE_TYPE),)
# add a local include dir to the global include path for kernel code
KERNEL_INCLUDES += $(MODULE_SRCDIR)/include
endif

# configure the module install path for 'usertest' and 'userapp' modules
# error out if these modules use the deprecated MODULE_DEPS variable
ifneq (,$(filter usertest%,$(MODULE_TYPE)))
MODULE_TYPE := $(MODULE_TYPE:usertest%=userapp%)
MODULE_INSTALL_PATH := test
ifneq ($(MODULE_DEPS),)
$(error $(MODULE) usertest modules must use MODULE_{LIBS,STATIC_LIBS}, not MODULE_DEPS)
endif
else ifneq (,$(filter userapp%,$(MODULE_TYPE)))
MODULE_INSTALL_PATH := bin
ifneq ($(MODULE_DEPS),)
$(error $(MODULE) userapp modules must use MODULE_{LIBS,STATIC_LIBS}, not MODULE_DEPS)
endif
endif

# ensure that library deps are short-name style
$(foreach d,$(MODULE_LIBS),$(call modname-require-short,$(d)))
$(foreach d,$(MODULE_STATIC_LIBS),$(call modname-require-short,$(d)))
$(foreach d,$(MODULE_SO_LIBS),$(call modname-require-short,$(d)))
$(foreach d,$(MODULE_SO_STATIC_LIBS),$(call modname-require-short,$(d)))

# all library deps go on the deps list
MODULE_DEPS += $(MODULE_LIBS) $(MODULE_STATIC_LIBS) $(MODULE_SO_LIBS) $(MODULE_SO_STATIC_LIBS)

# all regular deps contribute to header deps list
MODULE_HEADER_DEPS += $(MODULE_DEPS)

# expand deps to canonical paths, remove dups
MODULE_DEPS := $(sort $(foreach d,$(MODULE_DEPS),$(call modname-make-canonical,$(d))))
MODULE_HEADER_DEPS := $(sort $(foreach d,$(MODULE_HEADER_DEPS),$(call modname-make-canonical,$(strip $(d)))))

# add the module deps to the global list
MODULES += $(MODULE_DEPS)

# compute our shortname, which has all of the build system prefix paths removed
MODULE_SHORTNAME = $(call modname-make-short,$(MODULE))

MODULE_BUILDDIR := $(call TOBUILDDIR,$(MODULE_SHORTNAME))

#$(info module $(MODULE))
#$(info MODULE_SRCDIR $(MODULE_SRCDIR))
#$(info MODULE_BUILDDIR $(MODULE_BUILDDIR))
#$(info MODULE_DEPS $(MODULE_DEPS))
#$(info MODULE_SRCS $(MODULE_SRCS))

MODULE_DEFINES += MODULE_COMPILEFLAGS=\"$(subst $(SPACE),_,$(MODULE_COMPILEFLAGS))\"
MODULE_DEFINES += MODULE_CFLAGS=\"$(subst $(SPACE),_,$(MODULE_CFLAGS))\"
MODULE_DEFINES += MODULE_CPPFLAGS=\"$(subst $(SPACE),_,$(MODULE_CPPFLAGS))\"
MODULE_DEFINES += MODULE_ASMFLAGS=\"$(subst $(SPACE),_,$(MODULE_ASMFLAGS))\"
MODULE_DEFINES += MODULE_OPTFLAGS=\"$(subst $(SPACE),_,$(MODULE_OPTFLAGS))\"
MODULE_DEFINES += MODULE_LDFLAGS=\"$(subst $(SPACE),_,$(MODULE_LDFLAGS))\"
MODULE_DEFINES += MODULE_SRCDEPS=\"$(subst $(SPACE),_,$(MODULE_SRCDEPS))\"
MODULE_DEFINES += MODULE_DEPS=\"$(subst $(SPACE),_,$(MODULE_DEPS))\"
MODULE_DEFINES += MODULE_LIBS=\"$(subst $(SPACE),_,$(MODULE_LIBS))\"
MODULE_DEFINES += MODULE_STATIC_LIBS=\"$(subst $(SPACE),_,$(MODULE_STATIC_LIBS))\"
MODULE_DEFINES += MODULE_SRCS=\"$(subst $(SPACE),_,$(MODULE_SRCS))\"
MODULE_DEFINES += MODULE_HEADER_DEPS=\"$(subst $(SPACE),_,$(MODULE_HEADER_DEPS))\"
MODULE_DEFINES += MODULE_TYPE=\"$(subst $(SPACE),_,$(MODULE_TYPE))\"

# Introduce local, libc, system, and dependency include paths
ifneq (,$(filter userapp% userlib,$(MODULE_TYPE)))
# user app
MODULE_SRCDEPS += $(USER_CONFIG_HEADER)
MODULE_COMPILEFLAGS += -Isystem/ulib/system/include
MODULE_COMPILEFLAGS += -I$(LOCAL_DIR)/include
MODULE_COMPILEFLAGS += -Ithird_party/ulib/musl/include
MODULE_COMPILEFLAGS += $(foreach DEP,$(MODULE_HEADER_DEPS),-I$(DEP)/include)
else
# kernel module
KERNEL_DEFINES += $(addsuffix =1,$(addprefix WITH_,$(MODULE_SHORTNAME)))
MODULE_SRCDEPS += $(KERNEL_CONFIG_HEADER)
endif

# generate a per-module config.h file
MODULE_CONFIG := $(MODULE_BUILDDIR)/config-module.h

# base name for the generated binaries, libraries, etc
MODULE_OUTNAME := $(MODULE_BUILDDIR)/$(notdir $(MODULE))

# base name for libraries
MODULE_LIBNAME := $(MODULE_BUILDDIR)/lib$(notdir $(MODULE))

# record so we can find for link resolution later
MODULE_$(MODULE)_OUTNAME := $(MODULE_OUTNAME)
MODULE_$(MODULE)_LIBNAME := $(MODULE_LIBNAME)

$(MODULE_CONFIG): MODULE_DEFINES:=$(MODULE_DEFINES)
$(MODULE_CONFIG): configheader
	@$(call MAKECONFIGHEADER,$@,MODULE_DEFINES)

GENERATED += $(MODULE_CONFIG)

MODULE_COMPILEFLAGS += --include $(MODULE_CONFIG)

MODULE_SRCDEPS += $(MODULE_CONFIG)

# flag so the link process knows to handle shared apps specially
ifeq ($(MODULE_TYPE),userapp)
MODULE_LDFLAGS += $(USERAPP_SHARED_LDFLAGS)
endif
ifeq ($(MODULE_TYPE),userapp-static)
MODULE_TYPE := userapp
MODULE_LDFLAGS += $(USERAPP_LDFLAGS)
endif

# include the rules to compile the module's object files
ifeq ($(MODULE_TYPE),)
# for kernel code
include make/compile.mk
else
# for userspace code
include make/ucompile.mk
endif

# MODULE_OBJS is passed back from compile.mk
#$(info MODULE_OBJS = $(MODULE_OBJS))

# build a ld -r style combined object
MODULE_OBJECT := $(MODULE_OUTNAME).mod.o
$(MODULE_OBJECT): $(MODULE_OBJS) $(MODULE_EXTRA_OBJS)
	@$(MKDIR)
	@echo linking $@
	$(NOECHO)$(LD) $(GLOBAL_MODULE_LDFLAGS) -r $^ -o $@

# track all of the source files compiled
ALLSRCS += $(MODULE_SRCS)

# track all the objects built
ALLOBJS += $(MODULE_OBJS)

# track the module object for make clean
GENERATED += $(MODULE_OBJECT)

# track the requested install name of the module
ifeq ($(MODULE_NAME),)
MODULE_NAME := $(basename $(notdir $(MODULE)))
endif

ifeq ($(MODULE_TYPE),)
ifneq ($(MODULE_LIBS),)
$(error $(MODULE) kernel modules may not use MODULE_LIBS)
endif
ifneq ($(MODULE_STATIC_LIBS),)
$(error $(MODULE) kernel modules may not use MODULE_STATIC_LIBS)
endif

# make the rest of the build depend on our output
ALLMODULE_OBJS := $(ALLMODULE_OBJS) $(MODULE_OBJECT)

else ifeq ($(MODULE_TYPE),userapp)
MODULE_USERAPP_OBJECT := $(patsubst %.mod.o,%.elf,$(MODULE_OBJECT))
ALLUSER_APPS += $(MODULE_USERAPP_OBJECT)
ALLUSER_MODULES += $(MODULE)
USER_MANIFEST_LINES += $(MODULE_INSTALL_PATH)/$(MODULE_NAME)=$(addsuffix .strip,$(MODULE_USERAPP_OBJECT))

MODULE_ALIBS := $(foreach lib,$(MODULE_STATIC_LIBS),$(call TOBUILDDIR,$(lib))/$(notdir $(lib)).mod.o)
MODULE_SOLIBS := $(foreach lib,$(MODULE_LIBS),$(call TOBUILDDIR,$(lib))/lib$(notdir $(lib)).so)

$(MODULE_USERAPP_OBJECT): _OBJS := $(USER_CRT1_OBJ) $(MODULE_OBJS) $(MODULE_EXTRA_OBJS)
$(MODULE_USERAPP_OBJECT): _LIBS := $(MODULE_ALIBS) $(MODULE_SOLIBS)
$(MODULE_USERAPP_OBJECT): _LDFLAGS := $(MODULE_LDFLAGS)
$(MODULE_USERAPP_OBJECT): $(USER_LINKER_SCRIPT) $(USER_CRT1_OBJ) $(MODULE_OBJS) $(MODULE_EXTRA_OBJS) $(MODULE_ALIBS) $(MODULE_SOLIBS)
	@$(MKDIR)
	@echo linking userapp $@
	$(NOECHO)$(LD) $(GLOBAL_LDFLAGS) $(ARCH_LDFLAGS) $(_LDFLAGS) \
		$(USER_LINKER_SCRIPT) $(_OBJS) $(_LIBS) $(LIBGCC) -o $@

else ifeq ($(MODULE_TYPE),userlib)

# modules that declare a soname desire to be shared libs
ifneq ($(MODULE_SO_NAME),)
MODULE_ALIBS := $(foreach lib,$(MODULE_SO_STATIC_LIBS),$(call TOBUILDDIR,$(lib))/lib$(notdir $(lib)).a)
MODULE_SOLIBS := $(foreach lib,$(MODULE_SO_LIBS),$(call TOBUILDDIR,$(lib))/lib$(notdir $(lib)).so)

$(MODULE_LIBNAME).so: _OBJS := $(MODULE_OBJS) $(MODULE_EXTRA_OBJS)
$(MODULE_LIBNAME).so: _LIBS := $(MODULE_ALIBS) $(MODULE_SOLIBS)
$(MODULE_LIBNAME).so: _SONAME := lib$(MODULE_SO_NAME).so
$(MODULE_LIBNAME).so: _LDFLAGS := $(MODULE_LDFLAGS)
$(MODULE_LIBNAME).so: $(MODULE_OBJS) $(MODULE_EXTRA_OBJS) $(MODULE_ALIBS) $(MODULE_SOLIBS)
	@$(MKDIR)
	@echo linking userlib $@
	$(NOECHO)$(LD) $(GLOBAL_LDFLAGS) $(USERLIB_SO_LDFLAGS) $(_LDFLAGS)\
		-shared -soname $(_SONAME) $(_OBJS) $(_LIBS) $(LIBGCC) -o $@

ALLUSER_LIBS += $(MODULE)
EXTRA_BUILDDEPS += $(MODULE_LIBNAME).so
GENERATED += $(MODULE_LIBNAME).so
USER_MANIFEST_LINES += lib/lib$(MODULE_SO_NAME).so=$(MODULE_LIBNAME).so.strip
endif

# default library objects
MODULE_LIBRARY_OBJS := $(MODULE_OBJS)

# build static library
$(MODULE_LIBNAME).a: $(MODULE_LIBRARY_OBJS)
	@$(MKDIR)
	@echo linking $@
	@rm -f $@
	$(NOECHO)$(AR) cr $@ $^

# always build all libraries
EXTRA_BUILDDEPS += $(MODULE_LIBNAME).a
GENERATED += $(MODULE_LIBNAME).a

# if the SYSROOT build feature is enabled, we will package
# up exported libraries, their headers, etc
ifeq ($(ENABLE_BUILD_SYSROOT),true)

ifneq ($(MODULE_SO_NAME),)
TMP := $(BUILDDIR)/sysroot/lib/lib$(MODULE_SO_NAME).so
$(call copy-dst-src,$(TMP),$(MODULE_LIBNAME).so)
SYSROOT_DEPS += $(TMP)
GENERATED += $(TMP)
endif

ifneq ($(MODULE_EXPORT),)
ifneq ($(MODULE_EXPORT),system)
TMP := $(BUILDDIR)/sysroot/lib/lib$(MODULE_EXPORT).a
$(call copy-dst-src,$(TMP),$(MODULE_LIBNAME).a)
SYSROOT_DEPS += $(TMP)
GENERATED += $(TMP)
endif
endif

# only install headers for exported libraries
ifneq ($(MODULE_EXPORT)$(MODULE_SO_NAME),)
# for now, unify all headers in one pile
# TODO: ddk, etc should be packaged separately
MODULE_INSTALL_HEADERS := $(BUILDDIR)/sysroot/include

# locate headers from module source public include dir
MODULE_PUBLIC_HEADERS := $(shell test -d $(MODULE_SRCDIR)/include && find $(MODULE_SRCDIR)/include -name \*\.h -or -name \*\.inc)
MODULE_PUBLIC_HEADERS := $(patsubst $(MODULE_SRCDIR)/include/%,%,$(MODULE_PUBLIC_HEADERS))

# translate them to the final destination
MODULE_PUBLIC_HEADERS := $(patsubst %,$(MODULE_INSTALL_HEADERS)/%,$(MODULE_PUBLIC_HEADERS))

# generate rules to copy them
$(call copy-dst-src,$(MODULE_INSTALL_HEADERS)/%.h,$(MODULE_SRCDIR)/include/%.h)
$(call copy-dst-src,$(MODULE_INSTALL_HEADERS)/%.inc,$(MODULE_SRCDIR)/include/%.inc)

SYSROOT_DEPS += $(MODULE_PUBLIC_HEADERS)
GENERATED += $(MODULE_PUBLIC_HEADERS)
endif

endif # if ENABLE_BUILD_SYSROOT true
endif # if MODULE_TYPE userlib

# empty out any vars set here
MODULE :=
MODULE_SHORTNAME :=
MODULE_SRCDIR :=
MODULE_BUILDDIR :=
MODULE_DEPS :=
MODULE_HEADER_DEPS :=
MODULE_SRCS :=
MODULE_OBJS :=
MODULE_DEFINES :=
MODULE_OPTFLAGS :=
MODULE_COMPILEFLAGS :=
MODULE_CFLAGS :=
MODULE_CPPFLAGS :=
MODULE_ASMFLAGS :=
MODULE_LDFLAGS :=
MODULE_SRCDEPS :=
MODULE_EXTRA_OBJS :=
MODULE_CONFIG :=
MODULE_OBJECT :=
MODULE_TYPE :=
MODULE_NAME :=
MODULE_EXPORT :=
MODULE_LIBS :=
MODULE_STATIC_LIBS :=
MODULE_SO_LIBS :=
MODULE_SO_STATIC_LIBS :=
MODULE_SO_NAME :=
