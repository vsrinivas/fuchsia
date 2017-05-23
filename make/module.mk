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
#               "driver" for Magenta driver, "" for standard LK module
# MODULE_LIBS : shared libraries for a userapp or userlib to depend on
# MODULE_STATIC_LIBS : static libraries for a userapp or userlib to depend on
# MODULE_SO_NAME : linkage name for the shared library

# the minimum module rules.mk file is as follows:
#
# LOCAL_DIR := $(GET_LOCAL_DIR)
# MODULE := $(LOCAL_DIR)
#
# MODULE_SRCS := $(LOCAL_DIR)/at_least_one_source_file.c
#
# include make/module.mk

# Remove any .postfix bits for submodules to find our source directory
MODULE_SRCDIR := $(firstword $(subst .,$(SPACE),$(MODULE)))

# If there's not a rules.mk that corresponds to our srcdir,
# something fishy is going on
ifeq ($(wildcard $(MODULE_SRCDIR)/rules.mk),)
$(error Module '$(MODULE)' missing $(MODULE_SRCDIR)/rules.mk)
endif

# Catch the "defined a module twice" failure case as soon
# as possible, so it's easier to understand.
ifneq ($(filter $(MODULE),$(DUPMODULES)),)
$(error Module '$(MODULE)' defined in multiple rules.mk files)
endif
DUPMODULES += $(MODULE)

# all library deps go on the deps list
_MODULE_DEPS := $(MODULE_DEPS) $(MODULE_LIBS) $(MODULE_STATIC_LIBS)

# Catch the depends on nonexistant module error case
# here where we can tell you what module has the bad deps
$(foreach mod,$(_MODULE_DEPS) $(MODULE_HEADER_DEPS),$(if $(wildcard $(mod)),,\
$(error Module '$(MODULE)' depends on '$(mod)' which does not exist)))

# all regular deps contribute to header deps list
MODULE_HEADER_DEPS += $(_MODULE_DEPS)

# use sort to de-duplicate our deps list
_MODULE_DEPS := $(sort $(_MODULE_DEPS))
MODULE_HEADER_DEPS := $(sort $(MODULE_HEADER_DEPS))

# add the module deps to the global list
MODULES += $(_MODULE_DEPS)

MODULE_BUILDDIR := $(call TOBUILDDIR,$(MODULE))

# MODULE_NAME is used to generate installed names
# it defaults to being derived from the MODULE directory
ifeq ($(MODULE_NAME),)
MODULE_NAME := $(lastword $(subst /,$(SPACE),$(MODULE)))
endif

# Introduce local, libc and dependency include paths
ifneq ($(MODULE_TYPE),)
ifneq ($(MODULE_TYPE),hostapp)
# user module
MODULE_SRCDEPS += $(USER_CONFIG_HEADER)
MODULE_COMPILEFLAGS += -Iglobal/include
MODULE_COMPILEFLAGS += -I$(LOCAL_DIR)/include
MODULE_COMPILEFLAGS += -Ithird_party/ulib/musl/include
MODULE_COMPILEFLAGS += $(foreach DEP,$(MODULE_HEADER_DEPS),-I$(DEP)/include)
MODULE_DEFINES += MODULE_LIBS=\"$(subst $(SPACE),_,$(MODULE_LIBS))\"
MODULE_DEFINES += MODULE_STATIC_LIBS=\"$(subst $(SPACE),_,$(MODULE_STATIC_LIBS))\"
endif
#TODO: is this right?
MODULE_SRCDEPS += $(USER_CONFIG_HEADER)
else
# kernel module
# add a local include dir to the global include path for kernel code
KERNEL_INCLUDES += $(MODULE_SRCDIR)/include
KERNEL_DEFINES += $(addsuffix =1,$(addprefix WITH_,$(patsubst third_party/%,%,$(patsubst kernel/%,%,$(MODULE)))))
MODULE_SRCDEPS += $(KERNEL_CONFIG_HEADER)
endif

#$(info module $(MODULE))
#$(info MODULE_SRCDIR $(MODULE_SRCDIR))
#$(info MODULE_BUILDDIR $(MODULE_BUILDDIR))
#$(info MODULE_DEPS $(MODULE_DEPS))
#$(info MODULE_SRCS $(MODULE_SRCS))

MODULE_DEFINES += MODULE_COMPILEFLAGS=\"$(subst $(SPACE),_,$(sort $(MODULE_COMPILEFLAGS)))\"
MODULE_DEFINES += MODULE_CFLAGS=\"$(subst $(SPACE),_,$(sort $(MODULE_CFLAGS)))\"
MODULE_DEFINES += MODULE_CPPFLAGS=\"$(subst $(SPACE),_,$(sort $(MODULE_CPPFLAGS)))\"
MODULE_DEFINES += MODULE_ASMFLAGS=\"$(subst $(SPACE),_,$(sort $(MODULE_ASMFLAGS)))\"
MODULE_DEFINES += MODULE_OPTFLAGS=\"$(subst $(SPACE),_,$(sort $(MODULE_OPTFLAGS)))\"
MODULE_DEFINES += MODULE_LDFLAGS=\"$(subst $(SPACE),_,$(sort $(MODULE_LDFLAGS)))\"
MODULE_DEFINES += MODULE_SRCDEPS=\"$(subst $(SPACE),_,$(sort $(MODULE_SRCDEPS)))\"
MODULE_DEFINES += MODULE_DEPS=\"$(subst $(SPACE),_,$(sort $(MODULE_DEPS)))\"
MODULE_DEFINES += MODULE_SRCS=\"$(subst $(SPACE),_,$(sort $(MODULE_SRCS)))\"
MODULE_DEFINES += MODULE_HEADER_DEPS=\"$(subst $(SPACE),_,$(sort $(MODULE_HEADER_DEPS)))\"
MODULE_DEFINES += MODULE_TYPE=\"$(subst $(SPACE),_,$(MODULE_TYPE))\"

# generate a per-module config.h file
MODULE_CONFIG := $(MODULE_BUILDDIR)/config-module.h

# base name for the generated binaries, libraries, etc
MODULE_OUTNAME := $(MODULE_BUILDDIR)/$(notdir $(MODULE))

# base name for libraries
MODULE_LIBNAME := $(MODULE_BUILDDIR)/lib$(notdir $(MODULE))

$(MODULE_CONFIG): MODULE_DEFINES:=$(MODULE_DEFINES)
$(MODULE_CONFIG): FORCE
	@$(call MAKECONFIGHEADER,$@,MODULE_DEFINES)

GENERATED += $(MODULE_CONFIG)

MODULE_COMPILEFLAGS += -include $(MODULE_CONFIG)

MODULE_SRCDEPS += $(MODULE_CONFIG)

# include compile rules appropriate to module type
# typeless modules are kernel modules
ifeq ($(MODULE_TYPE),)
include make/compile.mk
else
ifeq ($(MODULE_TYPE),hostapp)
include make/hcompile.mk
else
include make/ucompile.mk
endif
endif

# MODULE_OBJS is passed back from *compile.mk
#$(info MODULE_OBJS = $(MODULE_OBJS))

# track all of the source files compiled
ALLSRCS += $(MODULE_SRCS)

# track all the objects built
ALLOBJS += $(MODULE_OBJS)

ifneq ($(MODULE_TYPE),hostapp)
ALL_TARGET_OBJS += $(MODULE_OBJS)
endif

# generate an input linker script for all kernel and user modules
ifneq ($(MODULE_TYPE),hostapp)
MODULE_OBJECT := $(MODULE_OUTNAME).mod.o
$(MODULE_OBJECT): $(MODULE_OBJS) $(MODULE_EXTRA_OBJS)
	@$(MKDIR)
	$(call BUILDECHO,linking $@)
	$(NOECHO)echo "INPUT($^)" > $@

# track the module object for make clean
GENERATED += $(MODULE_OBJECT)
endif

ifeq ($(MODULE_TYPE),)
# modules with no type are kernel modules
ifneq ($(MODULE_LIBS)$(MODULE_STATIC_LIBS),)
$(error $(MODULE) kernel modules may not use MODULE_LIBS or MODULE_STATIC_LIBS)
endif
# make the rest of the build depend on our output
ALLMODULE_OBJS := $(ALLMODULE_OBJS) $(MODULE_OBJECT)
else
# otherwise they are some named module flavor
include make/module-$(patsubst %-static,%,$(MODULE_TYPE)).mk
endif




# empty out any vars set here
MODULE :=
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
MODULE_SO_NAME :=
MODULE_INSTALL_PATH :=
MODULE_SO_INSTALL_NAME :=
MODULE_HOST_LIBS :=
