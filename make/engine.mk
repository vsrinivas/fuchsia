# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_MAKEFILE:=$(MAKEFILE_LIST)

# include settings for prebuilts that are auto-updated by checkout scripts
-include prebuilt/config.mk

# try to include a file in the local dir to let the user semi-permanently set options
-include local.mk
include make/macros.mk

# various command line and environment arguments
# default them to something so when they're referenced in the make instance they're not undefined
BUILDROOT ?= .
DEBUG ?= 2
ENABLE_BUILD_LISTFILES ?= false
ENABLE_BUILD_SYSROOT ?= false
ENABLE_BUILD_LISTFILES := $(call TOBOOL,$(ENABLE_BUILD_LISTFILES))
ENABLE_BUILD_SYSROOT := $(call TOBOOL,$(ENABLE_BUILD_SYSROOT))
USE_CLANG ?= false
USE_LLD ?= $(USE_CLANG)
ifeq ($(call TOBOOL,$(USE_LLD)),true)
USE_GOLD := false
USE_LTO ?= false
USE_THINLTO ?= false
else
USE_GOLD ?= true
USE_LTO := false
USE_THINLTO := false
endif
LKNAME ?= magenta
CLANG_TARGET_FUCHSIA ?= false
USE_LINKER_GC ?= true


# If no build directory suffix has been explicitly supplied by the environment,
# generate a default based on build options.  Start with no suffix, then add
# "-clang" if we are building with clang, and "-release" if we are building with
# DEBUG=0
ifeq ($(origin BUILDDIR_SUFFIX),undefined)
BUILDDIR_SUFFIX :=

ifeq ($(call TOBOOL,$(USE_CLANG)),true)
BUILDDIR_SUFFIX := $(BUILDDIR_SUFFIX)-clang
endif

ifeq ($(call TOBOOL,$(DEBUG)),false)
BUILDDIR_SUFFIX := $(BUILDDIR_SUFFIX)-release
endif

endif   # if BUILDDIR_SUFFIX is empty

# special rule for handling make spotless
ifeq ($(MAKECMDGOALS),spotless)
spotless:
	rm -rf -- "$(BUILDROOT)"/build-*
else

# If one of our goals (from the commandline) happens to have a
# matching project/goal.mk, then we should re-invoke make with
# that project name specified...

project-name := $(firstword $(MAKECMDGOALS))

ifneq ($(project-name),)
ifneq ($(strip $(wildcard kernel/project/$(project-name).mk)),)
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
GENERATED_INCLUDES:=$(BUILDDIR)/gen/include
OUTLKBIN := $(BUILDDIR)/$(LKNAME).bin
OUTLKELF := $(BUILDDIR)/$(LKNAME).elf
GLOBAL_CONFIG_HEADER := $(BUILDDIR)/config-global.h
KERNEL_CONFIG_HEADER := $(BUILDDIR)/config-kernel.h
USER_CONFIG_HEADER := $(BUILDDIR)/config-user.h
GLOBAL_INCLUDES := system/public system/private $(GENERATED_INCLUDES)
GLOBAL_OPTFLAGS ?= $(ARCH_OPTFLAGS)
GLOBAL_DEBUGFLAGS ?= -g
GLOBAL_COMPILEFLAGS := $(GLOBAL_DEBUGFLAGS) -finline -include $(GLOBAL_CONFIG_HEADER)
GLOBAL_COMPILEFLAGS += -Wall -Wextra -Wno-multichar -Werror -Wno-error=deprecated-declarations
GLOBAL_COMPILEFLAGS += -Wno-unused-parameter -Wno-unused-function -Wno-unused-label -Werror=return-type
GLOBAL_COMPILEFLAGS += -fno-common
ifeq ($(call TOBOOL,$(USE_CLANG)),true)
GLOBAL_COMPILEFLAGS += -Wno-address-of-packed-member
# TODO(mcgrathr): This avoids complaints about the 'leaf' attribute, which
# GCC supports as an optimization hint but Clang does not grok.  This can
# be removed when https://llvm.org/bugs/show_bug.cgi?id=30980 is fixed.
GLOBAL_COMPILEFLAGS += -Wno-unknown-attributes
GLOBAL_COMPILEFLAGS += -Wthread-safety
else
GLOBAL_COMPILEFLAGS += -Wno-nonnull-compare
endif
GLOBAL_CFLAGS := -std=c11 -Werror-implicit-function-declaration -Wstrict-prototypes -Wwrite-strings
GLOBAL_CPPFLAGS := -std=c++14 -fno-exceptions -fno-rtti -fno-threadsafe-statics -Wconversion -Wno-sign-conversion
#GLOBAL_CPPFLAGS += -Weffc++
GLOBAL_ASMFLAGS := -DASSEMBLY
GLOBAL_LDFLAGS := -nostdlib --build-id
# $(addprefix -L,$(LKINC)) XXX
GLOBAL_MODULE_LDFLAGS :=

# By default the sysroot is generated in "sysroot" under
# the build directory, but this is overrideable
ifeq ($(BUILDSYSROOT),)
BUILDSYSROOT := $(BUILDDIR)/sysroot
else
# be noisy if we are
$(info BUILDSYSROOT = $(BUILDSYSROOT))
endif

# Kernel compile flags
KERNEL_INCLUDES := $(BUILDDIR) kernel/include
KERNEL_COMPILEFLAGS := -fno-pic -ffreestanding -include $(KERNEL_CONFIG_HEADER)
KERNEL_COMPILEFLAGS += -Wformat=2
ifeq ($(call TOBOOL,$(USE_CLANG)),false)
KERNEL_COMPILEFLAGS += -Wformat-signedness
endif
KERNEL_CFLAGS := -Wmissing-prototypes
KERNEL_CPPFLAGS :=
KERNEL_ASMFLAGS :=

# Build flags for modules that want frame pointers.
# crashlogger, ngunwind, backtrace use this so that the simplisitic unwinder
# will work with them. These are recorded here so that modules don't need
# knowledge of the details. They just need to do:
# MODULE_COMPILEFLAGS += $(KEEP_FRAME_POINTER_COMPILEFLAGS)
KEEP_FRAME_POINTER_COMPILEFLAGS := -fno-omit-frame-pointer

# User space compile flags
USER_COMPILEFLAGS := -include $(USER_CONFIG_HEADER) -fPIC -D_ALL_SOURCE=1
USER_CFLAGS :=
USER_CPPFLAGS :=
USER_ASMFLAGS :=

# Additional flags for dynamic linking, both for dynamically-linked
# executables and for shared libraries.
USER_LDFLAGS := \
    -z combreloc -z relro -z now -z text \
    --hash-style=gnu --eh-frame-hdr

ifeq ($(call TOBOOL,$(USE_CLANG)),true)
ifeq ($(call TOBOOL,$(USE_LTO)),true)
ifeq ($(call TOBOOL,$(USE_THINLTO)),true)
USER_COMPILEFLAGS += -flto=thin
USER_LDFLAGS += --thinlto-jobs=8 --thinlto-cache-dir=$(BUILDDIR)/thinlto-cache
else
USER_COMPILEFLAGS += -flto -fwhole-program-vtables
# Full LTO doesn't require any special ld flags.
endif
endif
endif

ifeq ($(call TOBOOL,$(USE_CLANG)),true)
SAFESTACK := -fsanitize=safe-stack -fstack-protector-strong
NO_SAFESTACK := -fno-sanitize=safe-stack -fno-stack-protector
else
SAFESTACK :=
NO_SAFESTACK :=
endif

USER_COMPILEFLAGS += $(SAFESTACK)

USER_CRT1_OBJ := $(BUILDDIR)/system/ulib/crt1.o

# Additional flags for building shared libraries (ld -shared).
USERLIB_SO_LDFLAGS := $(USER_LDFLAGS) -z defs

# This is the string embedded into dynamically-linked executables
# as PT_INTERP.  The launchpad library looks this up via the
# "loader service", so it should be a simple name rather than an
# absolute pathname as is used for this on other systems.
USER_SHARED_INTERP := ld.so.1

# Additional flags for building dynamically-linked executables.
USERAPP_LDFLAGS := \
    $(USER_LDFLAGS) -pie -dynamic-linker $(USER_SHARED_INTERP)

ifeq ($(call TOBOOL,$(USE_GOLD)),false)
# BFD ld stupidly insists on resolving dependency DSO's symbols when
# doing a -shared -z defs link.  To do this it needs to find
# dependencies' dependencies, which requires -rpath-link.  Gold does
# not have this misfeature.  Since ulib/musl needs ulib/magenta and
# everything needs ulib/musl, this covers the actual needs in the
# build today without resorting to resolving inter-module dependencies
# to generate -rpath-link in a general fashion.  Eventually we should
# always use gold or lld for all the user-mode links, and then we'll
# never need this.
USERAPP_LDFLAGS += -rpath-link $(BUILDDIR)/ulib/magenta
endif

# Architecture specific compile flags
ARCH_COMPILEFLAGS :=
ARCH_CFLAGS :=
ARCH_CPPFLAGS :=
ARCH_ASMFLAGS :=

# top level rule
all:: $(OUTLKBIN) $(OUTLKELF)-gdb.py

ifeq ($(ENABLE_BUILD_LISTFILES),true)
all:: $(OUTLKELF).lst $(OUTLKELF).debug.lst  $(OUTLKELF).sym $(OUTLKELF).sym.sorted $(OUTLKELF).size
endif

# master module object list
ALLOBJS_MODULE :=

# all module objects for the target (does not include hostapp)
ALL_TARGET_OBJS :=

# master object list (for dep generation)
ALLOBJS :=

# master source file list
ALLSRCS :=

# a linker script needs to be declared in one of the project/target/platform files
LINKER_SCRIPT :=

# anything you add here will be deleted in make clean
GENERATED :=

# anything added to GLOBAL_DEFINES will be put into $(BUILDDIR)/config-global.h
GLOBAL_DEFINES :=

# anything added to KERNEL_DEFINES will be put into $(BUILDDIR)/config-kernel.h
KERNEL_DEFINES := LK=1 _KERNEL=1

# anything added to USER_DEFINES will be put into $(BUILDDIR)/config-user.h
USER_DEFINES :=

# Anything added to GLOBAL_SRCDEPS will become a dependency of every source file in the system.
# Useful for header files that may be included by one or more source files.
GLOBAL_SRCDEPS := $(GLOBAL_CONFIG_HEADER)

# Anything added to TARGET_SRCDEPS will become a dependency of every target module file in the system.
# Useful for header files that may be included by one or more source files.
TARGET_MODDEPS :=

# these need to be filled out by the project/target/platform rules.mk files
TARGET :=
PLATFORM :=
ARCH :=
ALLMODULES :=

# this is the *true* allmodules, to check for duplicate modules
# (since submodules do not contribute to ALLMODULES)
DUPMODULES :=

# add any external module dependencies
MODULES := $(EXTERNAL_MODULES)

# any .mk specified here will be included before build.mk
EXTRA_BUILDRULES :=

# any rules you put here will also be built by the system before considered being complete
EXTRA_BUILDDEPS :=

# any rules you put here will be depended on in clean builds
EXTRA_CLEANDEPS :=

# build ids
EXTRA_IDFILES :=

# any objects you put here get linked with the final image
EXTRA_OBJS :=

# userspace apps to build and include in initfs
ALLUSER_APPS :=

# userspace app modules
ALLUSER_MODULES :=

# userspace lib modules
ALLUSER_LIBS :=

# host apps to build
ALLHOST_APPS :=

# sysroot (exported libraries and headers)
SYSROOT_DEPS :=

# MDI source files used to generate the mdi.bin binary blob
MDI_SRCS :=

# MDI source files used to generate the mdi-defs.h header file
MDI_INCLUDES :=

# For now always enable frame pointers so kernel backtraces
# can work and define WITH_PANIC_BACKTRACE to enable them in panics
# MG-623
KERNEL_DEFINES += WITH_PANIC_BACKTRACE=1 WITH_FRAME_POINTERS=1
KERNEL_COMPILEFLAGS += $(KEEP_FRAME_POINTER_COMPILEFLAGS)

# userspace boot file system generated by the build system
USER_BOOTDATA := $(BUILDDIR)/bootdata.bin
USER_FS := $(BUILDDIR)/user.fs

# additional bootdata items to be included to bootdata.bin
ADDITIONAL_BOOTDATA_ITEMS :=

# manifest of files to include in the user bootfs
USER_MANIFEST := $(BUILDDIR)/bootfs.manifest
USER_MANIFEST_LINES :=
# The contents of this are derived from BOOTFS_DEBUG_MODULES.
USER_MANIFEST_DEBUG_INPUTS :=

# if someone defines this, the build id will be pulled into lib/version
BUILDID ?=

# set V=1 in the environment if you want to see the full command line of every command
ifeq ($(V),1)
NOECHO :=
else
NOECHO ?= @
endif

# used to force a rule to run every time
.PHONY: FORCE
FORCE:

# try to include the project file
-include kernel/project/$(PROJECT).mk
ifndef TARGET
$(error couldn't find project or project doesn't define target)
endif
include kernel/target/$(TARGET)/rules.mk
ifndef PLATFORM
$(error couldn't find target or target doesn't define platform)
endif
include kernel/platform/$(PLATFORM)/rules.mk

ifeq ($(call TOBOOL,$(QUIET)),false)
$(info PROJECT/PLATFORM/TARGET = $(PROJECT) / $(PLATFORM) / $(TARGET))
endif

include system/host/rules.mk
include kernel/arch/$(ARCH)/rules.mk
include kernel/top/rules.mk
include make/sysgen.mk

# recursively include any modules in the MODULE variable, leaving a trail of included
# modules in the ALLMODULES list
include make/recurse.mk

# rules for generating MDI header and binary
include make/mdi.mk

ifneq ($(EXTRA_IDFILES),)
$(BUILDDIR)/ids.txt: $(EXTRA_IDFILES)
	$(call BUILDECHO,generating $@)
	@rm -f -- "$@.tmp"
	@for f in $(EXTRA_IDFILES); do \
	echo `cat $$f` `echo $$f | sed 's/\.id$$//g'` >> $@.tmp; \
	done; \
	mv $@.tmp $@

EXTRA_BUILDDEPS += $(BUILDDIR)/ids.txt
GENERATED += $(BUILDDIR)/ids.txt
endif

ifeq ($(ENABLE_BUILD_SYSROOT),true)
# identify global headers to copy to the sysroot
GLOBAL_HEADERS := $(shell find system/public -name \*\.h -o -name \*\.inc)
SYSROOT_HEADERS := $(patsubst system/public/%,$(BUILDSYSROOT)/include/%,$(GLOBAL_HEADERS))

# generate rule to copy them
$(call copy-dst-src,$(BUILDSYSROOT)/include/%.h,system/public/%.h)
$(call copy-dst-src,$(BUILDSYSROOT)/include/%.inc,system/public/%.inc)

SYSROOT_DEPS += $(SYSROOT_HEADERS)
GENERATED += $(SYSROOT_HEADERS)

# copy crt*.o files to the sysroot
# crt1.o is temporary as we'll stop supporting fully static linking
SYSROOT_CRT1 := $(BUILDSYSROOT)/lib/crt1.o
$(call copy-dst-src,$(SYSROOT_CRT1),$(USER_CRT1_OBJ))
SYSROOT_SCRT1 := $(BUILDSYSROOT)/lib/Scrt1.o
$(call copy-dst-src,$(SYSROOT_SCRT1),$(USER_CRT1_OBJ))
SYSROOT_DEPS += $(SYSROOT_CRT1) $(SYSROOT_SCRT1)
GENERATED += $(SYSROOT_CRT1) $(SYSROOT_SCRT1)

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

SYSROOT_DEPS += $(BUILDSYSROOT)/lib/libm.so $(BUILDSYSROOT)/lib/libdl.so $(BUILDSYSROOT)/lib/libpthread.so
GENERATED += $(BUILDSYSROOT)/lib/libm.so $(BUILDSYSROOT)/lib/libdl.so $(BUILDSYSROOT)/lib/libpthread.so

# GDB specifically looks for ld.so.1, so we create that as a symlink.
$(BUILDSYSROOT)/debug-info/$(USER_SHARED_INTERP): FORCE
	@$(MKDIR)
	$(NOECHO)rm -f $@
	$(NOECHO)ln -s libc.so $@

SYSROOT_DEPS += $(BUILDSYSROOT)/debug-info/$(USER_SHARED_INTERP)
GENERATED += $(BUILDSYSROOT)/debug-info/$(USER_SHARED_INTERP)

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
	    $(if $(filter false,$(call TOBOOL,$(QUIET))),\
	    	 echo generating $(@:.stamp=);) \
	    mv $(@:.stamp=.new) $(@:.stamp=); \
	fi
	$(NOECHO)touch $@

GENERATED += $(BUILDDIR)/sysroot.list $(BUILDDIR)/sysroot.list.stamp
EXTRA_BUILDDEPS += $(BUILDDIR)/sysroot.list.stamp
endif

EXTRA_BUILDDEPS += $(SYSROOT_DEPS)

# make the build depend on all of the user apps
all:: $(foreach app,$(ALLUSER_APPS),$(app) $(app).strip)

# and all host tools
all:: $(ALLHOST_APPS)

tools:: $(ALLHOST_APPS)

# add some automatic configuration defines
KERNEL_DEFINES += \
	PROJECT_$(PROJECT)=1 \
	PROJECT=\"$(PROJECT)\" \
	TARGET_$(TARGET)=1 \
	TARGET=\"$(TARGET)\" \
	PLATFORM_$(PLATFORM)=1 \
	PLATFORM=\"$(PLATFORM)\" \
	ARCH_$(ARCH)=1 \
	ARCH=\"$(ARCH)\" \

# debug build?
# TODO(johngro) : Make LK and MX debug levels independently controlable.
ifneq ($(DEBUG),)
GLOBAL_DEFINES += \
	LK_DEBUGLEVEL=$(DEBUG) \
	MX_DEBUGLEVEL=$(DEBUG)
endif

# allow additional defines from outside the build system
ifneq ($(EXTERNAL_DEFINES),)
GLOBAL_DEFINES += $(EXTERNAL_DEFINES)
$(info EXTERNAL_DEFINES = $(EXTERNAL_DEFINES))
endif

# Modules are added earlier before the recurse stage, so just print the info here
ifneq ($(EXTERNAL_MODULES),)
$(info EXTERNAL_MODULES = $(EXTERNAL_MODULES))
endif

ifneq ($(EXTERNAL_KERNEL_DEFINES),)
KERNEL_DEFINES += $(EXTERNAL_KERNEL_DEFINES)
$(info EXTERNAL_KERNEL_DEFINES = $(EXTERNAL_KERNEL_DEFINES))
endif

# prefix all of the paths in GLOBAL_INCLUDES and KERNEL_INCLUDES with -I
GLOBAL_INCLUDES := $(addprefix -I,$(GLOBAL_INCLUDES))
KERNEL_INCLUDES := $(addprefix -I,$(KERNEL_INCLUDES))

# set up paths to various tools
ifeq ($(call TOBOOL,$(USE_CLANG)),true)
CC := $(CLANG_TOOLCHAIN_PREFIX)clang
AR := $(CLANG_TOOLCHAIN_PREFIX)llvm-ar
OBJDUMP := $(CLANG_TOOLCHAIN_PREFIX)llvm-objdump
READELF := $(CLANG_TOOLCHAIN_PREFIX)llvm-readobj -elf-output-style=GNU
CPPFILT := $(CLANG_TOOLCHAIN_PREFIX)llvm-cxxfilt
SIZE := $(CLANG_TOOLCHAIN_PREFIX)llvm-size
NM := $(CLANG_TOOLCHAIN_PREFIX)llvm-nm
else
CC := $(TOOLCHAIN_PREFIX)gcc
AR := $(TOOLCHAIN_PREFIX)ar
OBJDUMP := $(TOOLCHAIN_PREFIX)objdump
READELF := $(TOOLCHAIN_PREFIX)readelf
CPPFILT := $(TOOLCHAIN_PREFIX)c++filt
SIZE := $(TOOLCHAIN_PREFIX)size
NM := $(TOOLCHAIN_PREFIX)nm
endif
LD := $(TOOLCHAIN_PREFIX)ld
ifeq ($(call TOBOOL,$(USE_LLD)),true)
LD := $(CLANG_TOOLCHAIN_PREFIX)ld.lld
endif
ifeq ($(call TOBOOL,$(USE_GOLD)),true)
USER_LD := $(LD).gold
else
USER_LD := $(LD)
endif
OBJCOPY := $(TOOLCHAIN_PREFIX)objcopy
STRIP := $(TOOLCHAIN_PREFIX)strip

LIBGCC := $(shell $(CC) $(GLOBAL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) -print-libgcc-file-name)
ifeq ($(LIBGCC),)
$(error cannot find runtime library, please set LIBGCC)
endif

# try to have the compiler output colorized error messages if available
export GCC_COLORS ?= 1

# setup host toolchain
# default to prebuilt clang
FOUND_HOST_GCC ?= $(shell which $(HOST_TOOLCHAIN_PREFIX)gcc)
HOST_TOOLCHAIN_PREFIX ?= $(CLANG_TOOLCHAIN_PREFIX)
HOST_USE_CLANG ?= $(shell which $(HOST_TOOLCHAIN_PREFIX)clang)
ifneq ($(HOST_USE_CLANG),)
HOST_CC      := $(HOST_TOOLCHAIN_PREFIX)clang
HOST_CXX     := $(HOST_TOOLCHAIN_PREFIX)clang++
HOST_AR      := $(HOST_TOOLCHAIN_PREFIX)llvm-ar
HOST_OBJDUMP := $(HOST_TOOLCHAIN_PREFIX)llvm-objdump
HOST_READELF := $(HOST_TOOLCHAIN_PREFIX)llvm-readobj
HOST_CPPFILT := $(HOST_TOOLCHAIN_PREFIX)llvm-cxxfilt
HOST_SIZE    := $(HOST_TOOLCHAIN_PREFIX)llvm-size
HOST_NM      := $(HOST_TOOLCHAIN_PREFIX)llvm-nm
HOST_LD      := $(HOST_TOOLCHAIN_PREFIX)lld-link
else
ifeq ($(FOUND_HOST_GCC),)
$(error cannot find toolchain, please set HOST_TOOLCHAIN_PREFIX or add it to your path)
endif
HOST_CC      := $(HOST_TOOLCHAIN_PREFIX)gcc
HOST_CXX     := $(HOST_TOOLCHAIN_PREFIX)g++
HOST_AR      := $(HOST_TOOLCHAIN_PREFIX)ar
HOST_OBJDUMP := $(HOST_TOOLCHAIN_PREFIX)objdump
HOST_READELF := $(HOST_TOOLCHAIN_PREFIX)readelf
HOST_CPPFILT := $(HOST_TOOLCHAIN_PREFIX)c++filt
HOST_SIZE    := $(HOST_TOOLCHAIN_PREFIX)size
HOST_NM      := $(HOST_TOOLCHAIN_PREFIX)nm
HOST_LD      := $(HOST_TOOLCHAIN_PREFIX)ld
endif
HOST_OBJCOPY := $(HOST_TOOLCHAIN_PREFIX)objcopy
HOST_STRIP   := $(HOST_TOOLCHAIN_PREFIX)strip

# Host compile flags
HOST_COMPILEFLAGS := -Wall -g -O2 -Isystem/public -Isystem/private -I$(GENERATED_INCLUDES)
HOST_CFLAGS := -std=c11
HOST_CPPFLAGS := -std=c++14 -fno-exceptions -fno-rtti
HOST_LDFLAGS :=
ifneq ($(HOST_USE_CLANG),)
# We need to use our provided libc++ and libc++abi (and their pthread
# dependency) rather than the host library. For host tools without
# C++, ignore the unused arguments.
HOST_CPPFLAGS += -stdlib=libc++
HOST_LDFLAGS += -stdlib=libc++ -static-libstdc++
# We don't need to link libc++abi.a on OS X.
ifneq ($(HOST_PLATFORM),darwin)
HOST_LDFLAGS += -Lprebuilt/downloads/clang+llvm-$(HOST_ARCH)-$(HOST_PLATFORM)/lib -Wl,-Bstatic -lc++abi -Wl,-Bdynamic -lpthread
endif
HOST_LDFLAGS += -Wno-unused-command-line-argument
endif
HOST_ASMFLAGS :=

ifneq ($(HOST_USE_CLANG),)
ifeq ($(HOST_PLATFORM),darwin)
HOST_SYSROOT ?= $(shell xcrun --show-sdk-path)
endif
endif

ifneq ($(HOST_SYSROOT),)
HOST_COMPILEFLAGS += --sysroot=$(HOST_SYSROOT)
endif

# tool locations
MKBOOTFS := $(BUILDDIR)/tools/mkbootfs
MDIGEN := $(BUILDDIR)/tools/mdigen

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
KERNEL_DEFINES += KERNEL_INCLUDES=\"$(subst $(SPACE),_,$(KERNEL_INCLUDES))\"
KERNEL_DEFINES += KERNEL_COMPILEFLAGS=\"$(subst $(SPACE),_,$(KERNEL_COMPILEFLAGS))\"
KERNEL_DEFINES += KERNEL_CFLAGS=\"$(subst $(SPACE),_,$(KERNEL_CFLAGS))\"
KERNEL_DEFINES += KERNEL_CPPFLAGS=\"$(subst $(SPACE),_,$(KERNEL_CPPFLAGS))\"
KERNEL_DEFINES += KERNEL_ASMFLAGS=\"$(subst $(SPACE),_,$(KERNEL_ASMFLAGS))\"
USER_DEFINES += USER_COMPILEFLAGS=\"$(subst $(SPACE),_,$(USER_COMPILEFLAGS))\"
USER_DEFINES += USER_CFLAGS=\"$(subst $(SPACE),_,$(USER_CFLAGS))\"
USER_DEFINES += USER_CPPFLAGS=\"$(subst $(SPACE),_,$(USER_CPPFLAGS))\"
USER_DEFINES += USER_ASMFLAGS=\"$(subst $(SPACE),_,$(USER_ASMFLAGS))\"
USER_DEFINES += USER_LDFLAGS=\"$(subst $(SPACE),_,$(USER_LDFLAGS))\"

#$(info LIBGCC = $(LIBGCC))
#$(info GLOBAL_COMPILEFLAGS = $(GLOBAL_COMPILEFLAGS))
#$(info GLOBAL_OPTFLAGS = $(GLOBAL_OPTFLAGS))

# bootloader (x86-64 only for now)
# This needs to be after CC et al are set above.
ifeq ($(ARCH),x86)
include bootloader/build.mk
endif

# make all object files depend on any targets in GLOBAL_SRCDEPS
$(ALLOBJS): $(GLOBAL_SRCDEPS)

# make all target object files depend on any targets in TARGET_MODDEPS
$(ALL_TARGET_OBJS): $(TARGET_MODDEPS)

# any extra top level build dependencies that someone may have declared
all:: $(EXTRA_BUILDDEPS)

clean: $(EXTRA_CLEANDEPS)
	rm -f $(ALLOBJS)
	rm -f $(DEPS)
	rm -f $(GENERATED)
	rm -f $(OUTLKBIN) $(OUTLKELF) $(OUTLKELF).lst $(OUTLKELF).debug.lst $(OUTLKELF).sym $(OUTLKELF).sym.sorted $(OUTLKELF).size $(OUTLKELF).hex $(OUTLKELF).dump $(OUTLKELF)-gdb.py
	rm -f $(foreach app,$(ALLUSER_APPS),$(app) $(app).lst $(app).dump $(app).strip)

install: all
	scp $(OUTLKBIN) 192.168.0.4:/tftproot

# generate a config-global.h file with all of the GLOBAL_DEFINES laid out in #define format
$(GLOBAL_CONFIG_HEADER): FORCE
	@$(call MAKECONFIGHEADER,$@,GLOBAL_DEFINES,"")

# generate a config-kernel.h file with all of the KERNEL_DEFINES laid out in #define format
$(KERNEL_CONFIG_HEADER): FORCE
	@$(call MAKECONFIGHEADER,$@,KERNEL_DEFINES,"")

# generate a config-user.h file with all of the USER_DEFINES laid out in #define format
$(USER_CONFIG_HEADER): FORCE
	@$(call MAKECONFIGHEADER,$@,USER_DEFINES,"#define __Fuchsia__ 1")

GENERATED += $(GLOBAL_CONFIG_HEADER) $(KERNEL_CONFIG_HEADER) $(USER_CONFIG_HEADER)

# Empty rule for the .d files. The above rules will build .d files as a side
# effect. Only works on gcc 3.x and above, however.
%.d:

ifeq ($(filter $(MAKECMDGOALS), clean), )
-include $(DEPS)
endif

endif

endif # make spotless
