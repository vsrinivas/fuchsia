# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_MAKEFILE:=$(MAKEFILE_LIST)

# try to include a file in the local dir to let the user semi-permanently set options
-include local.mk
include make/macros.mk

# various command line and environment arguments
# default them to something so when they're referenced in the make instance they're not undefined
BUILDROOT ?= .
BUILDDIR_SUFFIX ?=
DEBUG ?= 2
ENABLE_BUILD_LISTFILES ?= false
ENABLE_BUILD_SYSROOT ?= false
CLANG ?= 0
LKNAME ?= magenta

# special rule for handling make spotless
ifeq ($(MAKECMDGOALS),spotless)
spotless:
	rm -rf -- "$(BUILDROOT)"/build-*
else

ifndef LKROOT
$(error please define LKROOT to the root of the $(LKNAME) build system)
endif

# If one of our goals (from the commandline) happens to have a
# matching project/goal.mk, then we should re-invoke make with
# that project name specified...

project-name := $(firstword $(MAKECMDGOALS))

ifneq ($(project-name),)
ifneq ($(strip $(foreach d,$(LKINC),$(wildcard $(d)/project/$(project-name).mk))),)
do-nothing := 1
$(MAKECMDGOALS) _all: make-make
make-make:
	@PROJECT=$(project-name) $(MAKE) -rR -f $(LOCAL_MAKEFILE) $(filter-out $(project-name), $(MAKECMDGOALS))

.PHONY: make-make
endif
endif

# some additional rules to print some help
include make/help.mk

ifeq ($(do-nothing),)

ifeq ($(PROJECT),)

ifneq ($(DEFAULT_PROJECT),)
PROJECT := $(DEFAULT_PROJECT)
else
$(error No project specified. Use 'make list' for a list of projects or 'make help' for additional help)
endif
endif

BUILDDIR := $(BUILDROOT)/build-$(PROJECT)$(BUILDDIR_SUFFIX)
OUTLKBIN := $(BUILDDIR)/$(LKNAME).bin
OUTLKELF := $(BUILDDIR)/$(LKNAME).elf
GLOBAL_CONFIG_HEADER := $(BUILDDIR)/global_config.h
KERNEL_CONFIG_HEADER := $(BUILDDIR)/kernel_config.h
USER_CONFIG_HEADER := $(BUILDDIR)/user_config.h

GLOBAL_INCLUDES := $(BUILDDIR) $(addsuffix /include,$(LKINC))
GLOBAL_OPTFLAGS ?= $(ARCH_OPTFLAGS)
GLOBAL_COMPILEFLAGS := -g -finline -include $(GLOBAL_CONFIG_HEADER)
GLOBAL_COMPILEFLAGS += -Wall -Wextra -Wno-multichar -Werror -Wno-unused-parameter -Wno-unused-function -Wno-unused-label -Werror=return-type
ifeq ($(CLANG),1)
GLOBAL_COMPILEFLAGS += -Wno-error
endif
GLOBAL_CFLAGS := --std=c11 -Werror-implicit-function-declaration -Wstrict-prototypes -Wwrite-strings
# Note: Both -fno-exceptions and -fno-asynchronous-unwind-tables is needed
# in order to stop gcc from emitting .eh_frame (which is part of the loaded
# image by default).
GLOBAL_CPPFLAGS := --std=c++11 -fno-exceptions -fno-asynchronous-unwind-tables -fno-rtti -fno-threadsafe-statics -Wconversion
#GLOBAL_CPPFLAGS += -Weffc++
GLOBAL_ASMFLAGS := -DASSEMBLY
GLOBAL_LDFLAGS := -nostdlib

GLOBAL_LDFLAGS += $(addprefix -L,$(LKINC))

# Kernel compile flags
KERNEL_COMPILEFLAGS := -ffreestanding -include $(KERNEL_CONFIG_HEADER)
KERNEL_CFLAGS :=
KERNEL_CPPFLAGS :=
KERNEL_ASMFLAGS :=

# User space compile flags
USER_COMPILEFLAGS := -include $(USER_CONFIG_HEADER)
USER_CFLAGS :=
USER_CPPFLAGS :=
USER_ASMFLAGS :=

# Architecture specific compile flags
ARCH_COMPILEFLAGS :=
ARCH_CFLAGS :=
ARCH_CPPFLAGS :=
ARCH_ASMFLAGS :=

# top level rule
all:: $(OUTLKBIN) $(OUTLKELF)-gdb.py

ifeq ($(call TOBOOL,$(ENABLE_BUILD_LISTFILES)),true)
all:: $(OUTLKELF).lst $(OUTLKELF).debug.lst  $(OUTLKELF).sym $(OUTLKELF).sym.sorted $(OUTLKELF).size $(OUTLKELF).dump
endif

# master module object list
ALLOBJS_MODULE :=

# master object list (for dep generation)
ALLOBJS :=

# master source file list
ALLSRCS :=

# a linker script needs to be declared in one of the project/target/platform files
LINKER_SCRIPT :=

# anything you add here will be deleted in make clean
GENERATED :=

# anything added to GLOBAL_DEFINES will be put into $(BUILDDIR)/config.h
GLOBAL_DEFINES := LK=1

# anything added to KERNEL_DEFINES will be put into $(BUILDDIR)/kernel_config.h
KERNEL_DEFINES := _KERNEL=1

# anything added to USER_DEFINES will be put into $(BUILDDIR)/user_config.h
USER_DEFINES :=

# Anything added to GLOBAL_SRCDEPS will become a dependency of every source file in the system.
# Useful for header files that may be included by one or more source files.
GLOBAL_SRCDEPS := $(GLOBAL_CONFIG_HEADER)

# these need to be filled out by the project/target/platform rules.mk files
TARGET :=
PLATFORM :=
ARCH :=
ALLMODULES :=

# add any external module dependencies
MODULES := $(EXTERNAL_MODULES)

# any .mk specified here will be included before build.mk
EXTRA_BUILDRULES :=

# any rules you put here will also be built by the system before considered being complete
EXTRA_BUILDDEPS :=

# any rules you put here will be depended on in clean builds
EXTRA_CLEANDEPS :=

# any objects you put here get linked with the final image
EXTRA_OBJS :=

# userspace apps to build and include in initfs
ALLUSER_APPS :=

# userspace app modules
ALLUSER_MODULES :=

# sysroot (exported libraries and headers)
SYSROOT_DEPS :=
SYSROOT_MEGA_LIBC := c runtime magenta mxio
SYSROOT_MEGA_LIBC_OBJS :=

# userspace boot file system generated by the build system
USER_BOOTFS := $(BUILDDIR)/user.bootfs
USER_FS := $(BUILDDIR)/user.fs

# manifest of files to include in the user bootfs
USER_MANIFEST := $(BUILDDIR)/user.manifest
USER_MANIFEST_LINES :=

# construct a slightly prettier version of LKINC with . removed and trailing / added
# used in module.mk
LKPREFIXES := $(patsubst %,%/,$(filter-out .,$(LKINC)))

# if someone defines this, the build id will be pulled into lib/version
BUILDID ?=

# set V=1 in the environment if you want to see the full command line of every command
ifeq ($(V),1)
NOECHO :=
else
NOECHO ?= @
endif

# try to include the project file
-include project/$(PROJECT).mk
ifndef TARGET
$(error couldn't find project or project doesn't define target)
endif
include target/$(TARGET)/rules.mk
ifndef PLATFORM
$(error couldn't find target or target doesn't define platform)
endif
include platform/$(PLATFORM)/rules.mk

$(info PROJECT = $(PROJECT))
$(info PLATFORM = $(PLATFORM))
$(info TARGET = $(TARGET))

include arch/$(ARCH)/rules.mk
include top/rules.mk

# recursively include any modules in the MODULE variable, leaving a trail of included
# modules in the ALLMODULES list
include make/recurse.mk

ifeq ($(call TOBOOL,$(ENABLE_BUILD_SYSROOT)),true)
ifneq ($(SYSROOT_MEGA_LIBC),)
MEGA_LIBC := $(BUILDDIR)/sysroot/lib/libc.a
# this is a really awful hack, but makes everything
# "just work" until we get shared libraries and can
# do it right
$(MEGA_LIBC): $(SYSROOT_MEGA_LIBC_OBJS)
	@$(MKDIR)
	@echo linking $@
	$(NOECHO)$(AR) cr $@ $^

SYSROOT_DEPS += $(MEGA_LIBC)
GENERATED += $(MEGA_LIBC)
endif
endif

# any extra top level build dependencies that someone declared
all:: $(EXTRA_BUILDDEPS) $(SYSROOT_DEPS)

# make the build depend on all of the user apps
all:: $(foreach app,$(ALLUSER_APPS),$(app) $(app).strip)

ifeq ($(call TOBOOL,$(ENABLE_BUILD_LISTFILES)),true)
all:: $(foreach app,$(ALLUSER_APPS),$(app).lst $(app).dump)
endif

# generate linkage dependencies for userspace apps after
# all modules have been evaluated, so we can recursively
# expand the module dependencies and handle cases like
# APP A depends on LIB B which depends on LIB C
#
# Duplicates are removed from the list with $(sort), which
# works due to how we're linking binaries now.  If we shift
# to true .a files we'll need to get fancier here.
EXPAND_DEPS = $(1) $(foreach DEP,$(MODULE_$(1)_DEPS),$(call EXPAND_DEPS,$(DEP)))

GET_USERAPP_DEPS = $(sort $(foreach DEP,$(MODULE_$(1)_DEPS),$(call EXPAND_DEPS,$(DEP))))

GET_USERAPP_LIBS = $(foreach DEP,$(call GET_USERAPP_DEPS,$(1)),$(call TOBUILDDIR,$(DEP).mod.o))

# NOTE: Uses :: rules to set deps on the app binary
# A rule in build.mk sets the final link line
LINK_USERAPP = $(eval $(1):: $(2))$(eval $(1): _LIBS := $(2))

$(foreach app,$(ALLUSER_MODULES),\
	$(eval $(call LINK_USERAPP,\
	$(call TOBUILDDIR,$(app).elf),\
	$(call GET_USERAPP_LIBS,$(app)))))

# add some automatic configuration defines
GLOBAL_DEFINES += \
	PROJECT_$(PROJECT)=1 \
	PROJECT=\"$(PROJECT)\" \
	TARGET_$(TARGET)=1 \
	TARGET=\"$(TARGET)\" \
	PLATFORM_$(PLATFORM)=1 \
	PLATFORM=\"$(PLATFORM)\" \
	ARCH_$(ARCH)=1 \
	ARCH=\"$(ARCH)\" \

# debug build?
ifneq ($(DEBUG),)
KERNEL_DEFINES += \
	LK_DEBUGLEVEL=$(DEBUG)
endif

# allow additional defines from outside the build system
ifneq ($(EXTERNAL_DEFINES),)
GLOBAL_DEFINES += $(EXTERNAL_DEFINES)
$(info EXTERNAL_DEFINES = $(EXTERNAL_DEFINES))
endif

# prefix all of the paths in GLOBAL_INCLUDES with -I
GLOBAL_INCLUDES := $(addprefix -I,$(GLOBAL_INCLUDES))

# default to no ccache
CCACHE ?=
ifeq ($(CLANG),1)
CC := $(CCACHE) $(TOOLCHAIN_PREFIX)clang
AR := $(TOOLCHAIN_PREFIX)llvm-ar
else
CC := $(CCACHE) $(TOOLCHAIN_PREFIX)gcc
AR := $(TOOLCHAIN_PREFIX)ar
endif
LD := $(TOOLCHAIN_PREFIX)ld
OBJDUMP := $(TOOLCHAIN_PREFIX)objdump
OBJCOPY := $(TOOLCHAIN_PREFIX)objcopy
CPPFILT := $(TOOLCHAIN_PREFIX)c++filt
SIZE := $(TOOLCHAIN_PREFIX)size
NM := $(TOOLCHAIN_PREFIX)nm
STRIP := $(TOOLCHAIN_PREFIX)strip

# try to have the compiler output colorized error messages if available
export GCC_COLORS ?= 1

# the logic to compile and link stuff is in here
include make/build.mk

DEPS := $(ALLOBJS:%o=%d)

# put all of the build flags in various config.h files to force a rebuild if any change
GLOBAL_DEFINES += GLOBAL_INCLUDES=\"$(subst $(SPACE),_,$(GLOBAL_INCLUDES))\"
GLOBAL_DEFINES += GLOBAL_COMPILEFLAGS=\"$(subst $(SPACE),_,$(GLOBAL_COMPILEFLAGS))\"
GLOBAL_DEFINES += GLOBAL_OPTFLAGS=\"$(subst $(SPACE),_,$(GLOBAL_OPTFLAGS))\"
GLOBAL_DEFINES += GLOBAL_CFLAGS=\"$(subst $(SPACE),_,$(GLOBAL_CFLAGS))\"
GLOBAL_DEFINES += GLOBAL_CPPFLAGS=\"$(subst $(SPACE),_,$(GLOBAL_CPPFLAGS))\"
GLOBAL_DEFINES += GLOBAL_ASMFLAGS=\"$(subst $(SPACE),_,$(GLOBAL_ASMFLAGS))\"
GLOBAL_DEFINES += GLOBAL_LDFLAGS=\"$(subst $(SPACE),_,$(GLOBAL_LDFLAGS))\"
GLOBAL_DEFINES += ARCH_COMPILEFLAGS=\"$(subst $(SPACE),_,$(ARCH_COMPILEFLAGS))\"
GLOBAL_DEFINES += ARCH_CFLAGS=\"$(subst $(SPACE),_,$(ARCH_CFLAGS))\"
GLOBAL_DEFINES += ARCH_CPPFLAGS=\"$(subst $(SPACE),_,$(ARCH_CPPFLAGS))\"
GLOBAL_DEFINES += ARCH_ASMFLAGS=\"$(subst $(SPACE),_,$(ARCH_ASMFLAGS))\"
KERNEL_DEFINES += KERNEL_COMPILEFLAGS=\"$(subst $(SPACE),_,$(KERNEL_COMPILEFLAGS))\"
KERNEL_DEFINES += KERNEL_CFLAGS=\"$(subst $(SPACE),_,$(KERNEL_CFLAGS))\"
KERNEL_DEFINES += KERNEL_CPPFLAGS=\"$(subst $(SPACE),_,$(KERNEL_CPPFLAGS))\"
KERNEL_DEFINES += KERNEL_ASMFLAGS=\"$(subst $(SPACE),_,$(KERNEL_ASMFLAGS))\"
USER_DEFINES += USER_COMPILEFLAGS=\"$(subst $(SPACE),_,$(USER_COMPILEFLAGS))\"
USER_DEFINES += USER_CFLAGS=\"$(subst $(SPACE),_,$(USER_CFLAGS))\"
USER_DEFINES += USER_CPPFLAGS=\"$(subst $(SPACE),_,$(USER_CPPFLAGS))\"
USER_DEFINES += USER_ASMFLAGS=\"$(subst $(SPACE),_,$(USER_ASMFLAGS))\"

#$(info LIBGCC = $(LIBGCC))
#$(info GLOBAL_COMPILEFLAGS = $(GLOBAL_COMPILEFLAGS))
#$(info GLOBAL_OPTFLAGS = $(GLOBAL_OPTFLAGS))

# make all object files depend on any targets in GLOBAL_SRCDEPS
$(ALLOBJS): $(GLOBAL_SRCDEPS)

clean: $(EXTRA_CLEANDEPS)
	rm -f $(ALLOBJS)
	rm -f $(DEPS)
	rm -f $(GENERATED)
	rm -f $(OUTLKBIN) $(OUTLKELF) $(OUTLKELF).lst $(OUTLKELF).debug.lst $(OUTLKELF).sym $(OUTLKELF).sym.sorted $(OUTLKELF).size $(OUTLKELF).hex $(OUTLKELF).dump $(OUTLKELF)-gdb.py
	rm -f $(foreach app,$(ALLUSER_APPS),$(app) $(app).lst $(app).dump $(app).strip)

install: all
	scp $(OUTLKBIN) 192.168.0.4:/tftproot

# generate a global_config.h file with all of the GLOBAL_DEFINES laid out in #define format
globalconfigheader:

$(GLOBAL_CONFIG_HEADER): globalconfigheader
	@$(call MAKECONFIGHEADER,$@,GLOBAL_DEFINES)

# generate a kernel_config.h file with all of the KERNEL_DEFINES laid out in #define format
kernelconfigheader:

$(KERNEL_CONFIG_HEADER): kernelconfigheader
	@$(call MAKECONFIGHEADER,$@,KERNEL_DEFINES)

# generate a user_config.h file with all of the USER_DEFINES laid out in #define format
userconfigheader:

$(USER_CONFIG_HEADER): userconfigheader
	@$(call MAKECONFIGHEADER,$@,USER_DEFINES)

GENERATED += $(GLOBAL_CONFIG_HEADER) $(KERNEL_CONFIG_HEADER) $(USER_CONFIG_HEADER)

# Empty rule for the .d files. The above rules will build .d files as a side
# effect. Only works on gcc 3.x and above, however.
%.d:

ifeq ($(filter $(MAKECMDGOALS), clean), )
-include $(DEPS)
endif

.PHONY: configheader
endif

endif # make spotless
