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
DEBUG_HARD ?= false
ENABLE_BUILD_LISTFILES ?= false
ENABLE_BUILD_SYSROOT ?= false
ENABLE_BUILD_LISTFILES := $(call TOBOOL,$(ENABLE_BUILD_LISTFILES))
ENABLE_BUILD_SYSROOT := $(call TOBOOL,$(ENABLE_BUILD_SYSROOT))
ENABLE_DDK_DEPRECATIONS ?= false
ENABLE_NEW_BOOTDATA := true
DISABLE_UTEST ?= false
ENABLE_ULIB_ONLY ?= false
USE_ASAN ?= false
USE_SANCOV ?= false
USE_LTO ?= false
USE_THINLTO ?= $(USE_LTO)
USE_CLANG ?= $(firstword $(filter true,$(call TOBOOL,$(USE_ASAN)) $(call TOBOOL,$(USE_LTO))) false)
USE_LLD ?= $(USE_CLANG)
ifeq ($(call TOBOOL,$(USE_LLD)),true)
USE_GOLD := false
else
USE_GOLD ?= true
endif
THINLTO_CACHE_DIR ?= $(BUILDDIR)/thinlto-cache
CLANG_TARGET_FUCHSIA ?= false
USE_LINKER_GC ?= true
HOST_USE_ASAN ?= false

ifeq ($(call TOBOOL,$(ENABLE_ULIB_ONLY)),true)
ENABLE_BUILD_SYSROOT := false
ifeq (,$(strip $(TOOLS)))
$(error ENABLE_ULIB_ONLY=true requires TOOLS=build-.../tools on command line)
endif
endif

# If no build directory suffix has been explicitly supplied by the environment,
# generate a default based on build options.  Start with no suffix, then add
# "-clang" if we are building with clang, and "-release" if we are building with
# DEBUG=0
ifeq ($(origin BUILDDIR_SUFFIX),undefined)
BUILDDIR_SUFFIX :=

ifeq ($(call TOBOOL,$(USE_ASAN)),true)
BUILDDIR_SUFFIX := $(BUILDDIR_SUFFIX)-asan
else ifeq ($(call TOBOOL,$(USE_LTO)),true)
ifeq ($(call TOBOOL,$(USE_THINLTO)),true)
BUILDDIR_SUFFIX := $(BUILDDIR_SUFFIX)-thinlto
else
BUILDDIR_SUFFIX := $(BUILDDIR_SUFFIX)-lto
endif
else ifeq ($(call TOBOOL,$(USE_CLANG)),true)
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
ifneq ($(strip $(wildcard kernel/project/$(project-name).mk \
			  kernel/project/alias/$(project-name).mk)),)
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

# DEBUG_HARD enables limited optimizations and full debug symbols for use with gdb/lldb
ifeq ($(call TOBOOL,$(DEBUG_HARD)),true)
GLOBAL_DEBUGFLAGS := -O0 -g3
endif
GLOBAL_DEBUGFLAGS ?= -O2 -g

BUILDDIR := $(BUILDROOT)/build-$(PROJECT)$(BUILDDIR_SUFFIX)
GENERATED_INCLUDES:=$(BUILDDIR)/gen/global/include
ZIRCON_BOOTIMAGE := $(BUILDDIR)/zircon.zbi
KERNEL_ZBI := $(BUILDDIR)/kernel.zbi
KERNEL_ELF := $(BUILDDIR)/zircon.elf
KERNEL_IMAGE := $(BUILDDIR)/kernel-image.elf
GLOBAL_CONFIG_HEADER := $(BUILDDIR)/config-global.h
KERNEL_CONFIG_HEADER := $(BUILDDIR)/config-kernel.h
USER_CONFIG_HEADER := $(BUILDDIR)/config-user.h
HOST_CONFIG_HEADER := $(BUILDDIR)/config-host.h
GLOBAL_INCLUDES := system/public system/private $(GENERATED_INCLUDES)
GLOBAL_OPTFLAGS ?= $(ARCH_OPTFLAGS)
# When embedding source file locations in debugging information, by default
# the compiler will record the absolute path of the current directory and
# make everything relative to that.  Instead, we tell the compiler to map
# the current directory to $(DEBUG_BUILDROOT), which is the "relative"
# location of the zircon source tree (i.e. usually . in a standalone build).
DEBUG_BUILDROOT ?= $(BUILDROOT)
GLOBAL_COMPILEFLAGS := $(GLOBAL_DEBUGFLAGS)
GLOBAL_COMPILEFLAGS += -fdebug-prefix-map=$(shell pwd)=$(DEBUG_BUILDROOT)
GLOBAL_COMPILEFLAGS += -finline -include $(GLOBAL_CONFIG_HEADER)
GLOBAL_COMPILEFLAGS += -Wall -Wextra -Wno-multichar -Werror -Wno-error=deprecated-declarations
GLOBAL_COMPILEFLAGS += -Wno-unused-parameter -Wno-unused-function -Werror=unused-label -Werror=return-type
GLOBAL_COMPILEFLAGS += -fno-common
# kernel/include/lib/counters.h and kernel.ld depend on -fdata-sections.
GLOBAL_COMPILEFLAGS += -ffunction-sections -fdata-sections
ifeq ($(call TOBOOL,$(USE_CLANG)),true)
GLOBAL_COMPILEFLAGS += -nostdlibinc
GLOBAL_COMPILEFLAGS += -no-canonical-prefixes
GLOBAL_COMPILEFLAGS += -Wno-address-of-packed-member
GLOBAL_COMPILEFLAGS += -Wthread-safety
GLOBAL_COMPILEFLAGS += -Wimplicit-fallthrough
else
GLOBAL_COMPILEFLAGS += -Wno-nonnull-compare
# TODO(mcgrathr): New warning in GCC 7 biting a lot of code; figure it out.
GLOBAL_COMPILEFLAGS += -Wno-format-truncation
endif
GLOBAL_CFLAGS := -std=c11 -Werror-implicit-function-declaration -Wstrict-prototypes -Wwrite-strings
GLOBAL_CPPFLAGS := -std=c++14 -fno-exceptions -fno-rtti -fno-threadsafe-statics -Wconversion -Wno-sign-conversion
#GLOBAL_CPPFLAGS += -Weffc++
GLOBAL_ASMFLAGS :=
GLOBAL_LDFLAGS := -nostdlib --build-id -z noexecstack
ifeq ($(call TOBOOL,$(USE_LLD)),true)
GLOBAL_LDFLAGS += -color-diagnostics
endif
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
KERNEL_COMPILEFLAGS := -ffreestanding -include $(KERNEL_CONFIG_HEADER)
KERNEL_COMPILEFLAGS += -Wformat=2 -Wvla
# GCC supports "-Wformat-signedness" but Clang currently does not.
ifeq ($(call TOBOOL,$(USE_CLANG)),false)
KERNEL_COMPILEFLAGS += -Wformat-signedness
endif
KERNEL_CFLAGS := -Wmissing-prototypes
KERNEL_CPPFLAGS :=
KERNEL_ASMFLAGS :=
KERNEL_LDFLAGS :=

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
ifeq ($(call TOBOOL,$(ENABLE_DDK_DEPRECATIONS)),true)
USER_COMPILEFLAGS += -DENABLE_DDK_DEPRECATIONS=1
endif

# Additional flags for dynamic linking, both for dynamically-linked
# executables and for shared libraries.
USER_LDFLAGS := \
    -z combreloc -z relro -z now -z text \
    --hash-style=gnu --eh-frame-hdr

ifeq ($(call TOBOOL,$(USE_LLD)),true)
USER_LDFLAGS += -z rodynamic
RODSO_LDFLAGS :=
else
RODSO_LDFLAGS := -T scripts/rodso.ld
endif

# Turn on -fasynchronous-unwind-tables to get .eh_frame.
# This is necessary for unwinding through optimized code.
# The unwind information is part of the loaded binary. It's not that much space
# and it allows for unwinding of stripped binaries, pc -> source translation
# can be done offline with, e.g., scripts/symbolize.
USER_COMPILEFLAGS += -fasynchronous-unwind-tables

# TODO(ZX-2361): Remove frame pointers when libunwind and our tooling agree on
# unwind tables. Until then compile with frame pointers in debug builds to get
# high-quality backtraces.
ifeq ($(call TOBOOL,$(DEBUG)),true)
USER_COMPILEFLAGS += $(KEEP_FRAME_POINTER_COMPILEFLAGS)
endif

# We want .debug_frame for the kernel. ZX-62
# And we still want asynchronous unwind tables. Alas there's (currently) no way
# to achieve this with our GCC. At the moment we compile with
# -fno-omit-frame-pointer which is good because we link with -gc-sections which
# means .eh_frame gets discarded so GCC-built kernels don't have any unwind
# info (except for assembly - heh)!
# Assembler code has its own way of requesting .debug_frame vs .eh_frame with
# the .cfi_sections directive. Sigh.
KERNEL_COMPILEFLAGS += -fno-exceptions -fno-unwind-tables

ifeq ($(call TOBOOL,$(USE_CLANG)),true)
NO_SAFESTACK := -fno-sanitize=safe-stack -fno-stack-protector
NO_SANITIZERS := -fno-sanitize=all -fno-stack-protector
else
NO_SAFESTACK :=
NO_SANITIZERS :=
endif

USER_SCRT1_OBJ := $(BUILDDIR)/system/ulib/Scrt1.o

# Additional flags for building shared libraries (ld -shared).
USERLIB_SO_LDFLAGS := $(USER_LDFLAGS) -z defs

# This is the string embedded into dynamically-linked executables
# as PT_INTERP.  The launchpad library looks this up via the
# "loader service", so it should be a simple name rather than an
# absolute pathname as is used for this on other systems.
USER_SHARED_INTERP := ld.so.1

# Programs built with ASan use the ASan-supporting dynamic linker.
ifeq ($(call TOBOOL,$(USE_ASAN)),true)
USER_SHARED_INTERP := asan/$(USER_SHARED_INTERP)
endif

# Additional flags for building dynamically-linked executables.
USERAPP_LDFLAGS := \
    $(USER_LDFLAGS) -pie -dynamic-linker $(USER_SHARED_INTERP)

ifeq ($(call TOBOOL,$(USE_GOLD)),false)
# BFD ld stupidly insists on resolving dependency DSO's symbols when
# doing a -shared -z defs link.  To do this it needs to find
# dependencies' dependencies, which requires -rpath-link.  Gold does
# not have this misfeature.  Since ulib/musl needs ulib/zircon and
# everything needs ulib/musl, this covers the actual needs in the
# build today without resorting to resolving inter-module dependencies
# to generate -rpath-link in a general fashion.  Eventually we should
# always use gold or lld for all the user-mode links, and then we'll
# never need this.
USERAPP_LDFLAGS += -rpath-link $(BUILDDIR)/ulib/zircon
endif

# Architecture specific compile flags
ARCH_COMPILEFLAGS :=
ARCH_CFLAGS :=
ARCH_CPPFLAGS :=
ARCH_ASMFLAGS :=

# top level rule
all::

# master module object list
ALLOBJS_MODULE :=

# all module objects for the target (does not include hostapp)
ALL_TARGET_OBJS :=

# master object list (for dep generation)
ALLOBJS :=

# master source file list
ALLSRCS :=

# master list of packages for export
ALLPKGS :=

# anything you add here will be deleted in make clean
GENERATED :=

# anything added to GLOBAL_DEFINES will be put into $(BUILDDIR)/config-global.h
GLOBAL_DEFINES :=

# anything added to KERNEL_DEFINES will be put into $(BUILDDIR)/config-kernel.h
KERNEL_DEFINES := LK=1 _KERNEL=1 ZIRCON_TOOLCHAIN=1

# anything added to USER_DEFINES will be put into $(BUILDDIR)/config-user.h
USER_DEFINES := ZIRCON_TOOLCHAIN=1

# anything added to HOST_DEFINES will be put into $(BUILDDIR)/config-host.h
HOST_DEFINES :=

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

# any rules you put here will be built if the kernel is also being built
EXTRA_KERNELDEPS :=

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

# host libs to build
ALLHOST_LIBS :=

# EFI libs to build
ALLEFI_LIBS :=

# sysroot (exported libraries and headers)
SYSROOT_DEPS :=

# For now always enable frame pointers so kernel backtraces
# can work and define WITH_PANIC_BACKTRACE to enable them in panics
# ZX-623
KERNEL_DEFINES += WITH_PANIC_BACKTRACE=1 WITH_FRAME_POINTERS=1
KERNEL_COMPILEFLAGS += $(KEEP_FRAME_POINTER_COMPILEFLAGS)

# additional bootdata items to be included to bootdata.bin
ADDITIONAL_BOOTDATA_ITEMS :=

# manifest of files to include in the user bootfs
USER_MANIFEST := $(BUILDDIR)/bootfs.manifest
USER_MANIFEST_LINES :=
# The contents of this are derived from BOOTFS_DEBUG_MODULES.
USER_MANIFEST_DEBUG_INPUTS :=

# Directory in the bootfs where MODULE_FIRMWARE files go.
FIRMWARE_INSTALL_DIR := lib/firmware
# Directory in the source tree where MODULE_FIRMWARE files are found.
FIRMWARE_SRC_DIR := prebuilt/downloads/firmware
# TODO(mcgrathr): Force an absolute path for this so that every rhs in the
# manifest either starts with $(BUILDDIR) or is absolute.
# //scripts/build-zircon.sh needs this.
FIRMWARE_SRC_DIR := $(abspath $(FIRMWARE_SRC_DIR))

# if someone defines this, the build id will be pulled into lib/version
BUILDID ?=

# Tool locations.
TOOLS := $(BUILDDIR)/tools
FIDL := $(TOOLS)/fidlc
ABIGEN := $(TOOLS)/abigen
ZBI := $(TOOLS)/zbi

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
-include $(firstword $(wildcard kernel/project/$(PROJECT).mk \
				kernel/project/alias/$(PROJECT).mk))
ifndef TARGET
$(error couldn't find project "$(PROJECT)" or project doesn't define target)
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
include make/abigen.mk

ifeq ($(call TOBOOL,$(USE_CLANG)),true)
GLOBAL_COMPILEFLAGS += --target=$(CLANG_ARCH)-fuchsia
endif

ifeq ($(call TOBOOL,$(USE_LTO)),true)
ifeq ($(call TOBOOL,$(USE_CLANG)),false)
$(error USE_LTO requires USE_CLANG)
endif
ifeq ($(call TOBOOL,$(USE_LLD)),false)
$(error USE_LTO requires USE_LLD)
endif
# LTO doesn't store -mcmodel=kernel information in the bitcode files as it
# does for many other codegen options so we have to set it explicitly. This
# can be removed when https://bugs.llvm.org/show_bug.cgi?id=33306 is fixed.
KERNEL_LDFLAGS += $(patsubst -mcmodel=%,-mllvm -code-model=%,\
                  $(filter -mcmodel=%,$(KERNEL_COMPILEFLAGS)))
ifeq ($(call TOBOOL,$(USE_THINLTO)),true)
GLOBAL_COMPILEFLAGS += -flto=thin
GLOBAL_LDFLAGS += --thinlto-jobs=8 --thinlto-cache-dir=$(THINLTO_CACHE_DIR)
else
GLOBAL_COMPILEFLAGS += -flto -fwhole-program-vtables
# Full LTO doesn't require any special ld flags.
endif
endif

ifeq ($(call TOBOOL,$(USE_SANCOV)),true)
ifeq ($(call TOBOOL,$(USE_ASAN)),false)
$(error USE_SANCOV requires USE_ASAN)
endif
endif

ifeq ($(call TOBOOL,$(USE_ASAN)),true)
ifeq ($(call TOBOOL,$(USE_CLANG)),false)
$(error USE_ASAN requires USE_CLANG)
endif

# Compile all of userland with ASan.  ASan makes safe-stack superfluous
# and ASan reporting doesn't really grok safe-stack, so disable it.
# Individual modules can append $(NO_SANITIZERS) to counteract this.
USER_COMPILEFLAGS += -fsanitize=address -fno-sanitize=safe-stack

# The Clang toolchain includes a manifest for the shared libraries it provides.
# The right-hand sides are relative to the directory containing the manifest.
CLANG_MANIFEST := $(CLANG_TOOLCHAIN_PREFIX)../lib/$(CLANG_ARCH)-fuchsia.manifest
CLANG_MANIFEST_LINES := \
    $(subst =,=$(CLANG_TOOLCHAIN_PREFIX)../lib/,$(shell cat $(CLANG_MANIFEST)))
find-clang-solib = $(filter lib/$1=%,$(CLANG_MANIFEST_LINES))
# Every userland executable and shared library compiled with ASan
# needs to link with $(ASAN_SOLIB).  module-user{app,lib}.mk adds it
# to MODULE_EXTRA_OBJS so the linking target will depend on it.
ASAN_SONAME := libclang_rt.asan-$(CLANG_ARCH).so
ASAN_SOLIB_MANIFEST := $(call find-clang-solib,$(ASAN_SONAME))
ASAN_SOLIB := $(word 2,$(subst =, ,$(ASAN_SOLIB_MANIFEST)))
USER_MANIFEST_LINES += {core}$(ASAN_SOLIB_MANIFEST)

# The ASan runtime DSO depends on more DSOs from the toolchain.  We don't
# link against those, so we don't need any build-time dependencies on them.
# But we need them alongside the ASan runtime DSO in the bootfs.
find-clang-asan-solib = $(or $(call find-clang-solib,asan/$1), \
			     $(call find-clang-solib,$1))
ASAN_RUNTIME_SONAMES := libc++abi.so.1 libunwind.so.1
USER_MANIFEST_LINES += $(foreach soname,$(ASAN_RUNTIME_SONAMES),\
				 {core}$(call find-clang-asan-solib,$(soname)))
endif

ifeq ($(call TOBOOL,$(USE_SANCOV)),true)
# Compile all of userland with coverage.
USER_COMPILEFLAGS += -fsanitize-coverage=trace-pc-guard
NO_SANCOV := -fno-sanitize-coverage=trace-pc-guard
NO_SANITIZERS += $(NO_SANCOV)
else
NO_SANCOV :=
endif

# To use LibFuzzer, we need to provide it and its dependency to the linker
# since we're not using Clang and its '-fsanitize=fuzzer' flag as a driver to
# lld.  Additionally, we need to make sure the shared objects are available on
# the device.
ifeq ($(call TOBOOL,$(USE_ASAN)),true)
FUZZ_ANAME := libclang_rt.fuzzer-$(CLANG_ARCH).a
FUZZ_ALIB := $(shell $(CLANG_TOOLCHAIN_PREFIX)clang \
				 $(GLOBAL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS)\
				 -print-file-name=$(FUZZ_ANAME))

FUZZ_RUNTIME_SONAMES := libc++.so.2 libc++abi.so.1
FUZZ_RUNTIME_SOLIBS := $(foreach soname,$(FUZZ_RUNTIME_SONAMES),\
				 $(word 2,$(subst =, ,$(call find-clang-asan-solib,$(soname)))))

FUZZ_EXTRA_OBJS := $(FUZZ_ALIB) $(FUZZ_RUNTIME_SOLIBS)
else
FUZZ_EXTRA_OBJS :=
endif

# Save these for the first module.mk iteration to see.
SAVED_EXTRA_BUILDDEPS := $(EXTRA_BUILDDEPS)
SAVED_GENERATED := $(GENERATED)
SAVED_USER_MANIFEST_LINES := $(USER_MANIFEST_LINES)

# recursively include any modules in the MODULE variable, leaving a trail of included
# modules in the ALLMODULES list
include make/recurse.mk

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
GENERATED += $(EXTRA_IDFILES)
endif

# include some rules for generating sysroot/ and contents in the build dir
include make/sysroot.mk

# make the build depend on all of the user apps
all:: $(foreach app,$(ALLUSER_APPS),$(app) $(app).strip)

# and all host tools
all:: $(ALLHOST_APPS) $(ALLHOST_LIBS)

tools:: $(ALLHOST_APPS) $(ALLHOST_LIBS)

# meta rule for the kernel
.PHONY: kernel
kernel: $(KERNEL_ZBI) $(EXTRA_KERNELDEPS)
ifeq ($(ENABLE_BUILD_LISTFILES),true)
kernel: $(KERNEL_ELF).lst $(KERNEL_ELF).sym $(KERNEL_ELF).sym.sorted $(KERNEL_ELF).size
endif

ifeq ($(call TOBOOL,$(ENABLE_ULIB_ONLY)),false)
# add the kernel to the build
all:: kernel
else
# No kernel, but we want the bootfs.manifest listing the installed libraries.
all:: user-manifest
endif

# meta rule for building just packages
.PHONY: packages
packages: $(ALLPKGS) $(BUILDDIR)/export/manifest

$(BUILDDIR)/export/manifest: FORCE
	@$(call BUILDECHO,generating $@ ;)\
	$(MKDIR) ;\
	rm -f $@.tmp ;\
	(for p in $(sort $(notdir $(ALLPKGS))) ; do echo $$p ; done) > $@.tmp ;\
	$(call TESTANDREPLACEFILE,$@.tmp,$@)

# build depends on all packages
all:: packages

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
# TODO(johngro) : Make LK and ZX debug levels independently controlable.
ifneq ($(DEBUG),)
GLOBAL_DEFINES += \
    LK_DEBUGLEVEL=$(DEBUG) \
    ZX_DEBUGLEVEL=$(DEBUG)
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

# Path to the Goma compiler wrapper.  Defaults to using no wrapper.
GOMACC ?=

# set up paths to various tools
ifeq ($(call TOBOOL,$(USE_CLANG)),true)
CC := $(GOMACC) $(CLANG_TOOLCHAIN_PREFIX)clang
AR := $(CLANG_TOOLCHAIN_PREFIX)llvm-ar
OBJDUMP := $(CLANG_TOOLCHAIN_PREFIX)llvm-objdump
READELF := $(CLANG_TOOLCHAIN_PREFIX)llvm-readelf
CPPFILT := $(CLANG_TOOLCHAIN_PREFIX)llvm-cxxfilt
SIZE := $(CLANG_TOOLCHAIN_PREFIX)llvm-size
NM := $(CLANG_TOOLCHAIN_PREFIX)llvm-nm
OBJCOPY := $(CLANG_TOOLCHAIN_PREFIX)llvm-objcopy
STRIP := $(CLANG_TOOLCHAIN_PREFIX)llvm-objcopy --strip-sections
else
CC := $(GOMACC) $(TOOLCHAIN_PREFIX)gcc
AR := $(TOOLCHAIN_PREFIX)ar
OBJDUMP := $(TOOLCHAIN_PREFIX)objdump
READELF := $(TOOLCHAIN_PREFIX)readelf
CPPFILT := $(TOOLCHAIN_PREFIX)c++filt
SIZE := $(TOOLCHAIN_PREFIX)size
NM := $(TOOLCHAIN_PREFIX)nm
OBJCOPY := $(TOOLCHAIN_PREFIX)objcopy
STRIP := $(TOOLCHAIN_PREFIX)objcopy --strip-all
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

LIBGCC := $(shell $(CC) $(GLOBAL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) -print-libgcc-file-name)
ifeq ($(LIBGCC),)
$(error cannot find runtime library, please set LIBGCC)
endif

# try to have the compiler output colorized error messages if available
export GCC_COLORS ?= 1

# setup bootloader toolchain
ifeq ($(ARCH),x86)
EFI_ARCH := x86_64
else ifeq ($(ARCH),arm64)
EFI_ARCH := aarch64
endif

ifeq ($(call TOBOOL,$(USE_CLANG)),true)
EFI_AR := $(CLANG_TOOLCHAIN_PREFIX)llvm-ar
EFI_CC := $(CLANG_TOOLCHAIN_PREFIX)clang
EFI_CXX := $(CLANG_TOOLCHAIN_PREFIX)clang++
EFI_LD := $(CLANG_TOOLCHAIN_PREFIX)lld-link
EFI_COMPILEFLAGS := --target=$(EFI_ARCH)-windows-msvc
else
EFI_AR := $(TOOLCHAIN_PREFIX)ar
EFI_CC := $(TOOLCHAIN_PREFIX)gcc
EFI_CXX := $(TOOLCHAIN_PREFIX)g++
EFI_LD := $(TOOLCHAIN_PREFIX)ld
EFI_COMPILEFLAGS := -fPIE
endif

EFI_OPTFLAGS := -O2
EFI_COMPILEFLAGS += -fno-stack-protector
EFI_COMPILEFLAGS += -Wall
EFI_CFLAGS := -fshort-wchar -std=c99 -ffreestanding
ifeq ($(EFI_ARCH),x86_64)
EFI_CFLAGS += -mno-red-zone
endif


# setup host toolchain
# default to prebuilt clang
FOUND_HOST_GCC ?= $(shell which $(HOST_TOOLCHAIN_PREFIX)gcc)
HOST_TOOLCHAIN_PREFIX ?= $(CLANG_TOOLCHAIN_PREFIX)
HOST_USE_CLANG ?= $(shell which $(HOST_TOOLCHAIN_PREFIX)clang)
ifneq ($(HOST_USE_CLANG),)
HOST_CC      := $(GOMACC) $(HOST_TOOLCHAIN_PREFIX)clang
HOST_CXX     := $(GOMACC) $(HOST_TOOLCHAIN_PREFIX)clang++
HOST_AR      := $(HOST_TOOLCHAIN_PREFIX)llvm-ar
else
ifeq ($(FOUND_HOST_GCC),)
$(error cannot find toolchain, please set HOST_TOOLCHAIN_PREFIX or add it to your path)
endif
HOST_CC      := $(GOMACC) $(HOST_TOOLCHAIN_PREFIX)gcc
HOST_CXX     := $(GOMACC) $(HOST_TOOLCHAIN_PREFIX)g++
HOST_AR      := $(HOST_TOOLCHAIN_PREFIX)ar
endif

# Host compile flags
HOST_COMPILEFLAGS := -g -O2 -Isystem/public -Isystem/private -I$(GENERATED_INCLUDES)
HOST_COMPILEFLAGS += -Wall -Wextra
HOST_COMPILEFLAGS += -Wno-unused-parameter -Wno-sign-compare
HOST_CFLAGS := -std=c11
HOST_CPPFLAGS := -std=c++14 -fno-exceptions -fno-rtti
HOST_LDFLAGS :=
ifneq ($(HOST_USE_CLANG),)
# We need to use our provided libc++ and libc++abi (and their pthread
# dependency) rather than the host library. The only exception is the
# case when we are cross-compiling the host tools in which case we use
# the C++ library from the sysroot.
# TODO(TC-78): This can be removed once the Clang
# toolchain ships with a cross-compiled C++ runtime.
ifeq ($(HOST_TARGET),)
ifeq ($(HOST_PLATFORM),linux)
ifeq ($(HOST_ARCH),x86_64)
HOST_SYSROOT ?= $(SYSROOT_linux-amd64_PATH)
else ifeq ($(HOST_ARCH),aarch64)
HOST_SYSROOT ?= $(SYSROOT_linux-arm64_PATH)
endif
# TODO(TC-77): Using explicit sysroot currently overrides location of C++
# runtime so we need to explicitly add it here.
HOST_LDFLAGS += -Lprebuilt/downloads/clang/lib
# The implicitly linked static libc++.a depends on these.
HOST_LDFLAGS += -ldl -lpthread
endif
endif
HOST_LDFLAGS += -static-libstdc++
# For host tools without C++, ignore the unused arguments.
HOST_LDFLAGS += -Wno-unused-command-line-argument
endif
HOST_ASMFLAGS :=

ifneq ($(HOST_TARGET),)
HOST_COMPILEFLAGS += --target=$(HOST_TARGET)
ifeq ($(HOST_TARGET),x86_64-linux-gnu)
HOST_SYSROOT ?= $(SYSROOT_linux-amd64_PATH)
else ifeq ($(HOST_TARGET),aarch64-linux-gnu)
HOST_SYSROOT ?= $(SYSROOT_linux-arm64_PATH)
endif
endif

ifneq ($(HOST_USE_CLANG),)
ifeq ($(HOST_PLATFORM),darwin)
HOST_SYSROOT ?= $(shell xcrun --show-sdk-path)
endif
endif

ifneq ($(HOST_SYSROOT),)
HOST_COMPILEFLAGS += --sysroot=$(HOST_SYSROOT)
endif

ifeq ($(call TOBOOL,$(HOST_USE_ASAN)),true)
HOST_COMPILEFLAGS += -fsanitize=address
export ASAN_SYMBOLIZER_PATH := $(HOST_TOOLCHAIN_PREFIX)llvm-symbolizer
endif

# the logic to compile and link stuff is in here
include make/build.mk

# top level target to just build the bootloader
.PHONY: bootloader
bootloader:

# build a bootloader if needed
include bootloader/build.mk

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
KERNEL_DEFINES += KERNEL_LDFLAGS=\"$(subst $(SPACE),_,$(KERNEL_LDFLAGS))\"

USER_DEFINES += USER_COMPILEFLAGS=\"$(subst $(SPACE),_,$(USER_COMPILEFLAGS))\"
USER_DEFINES += USER_CFLAGS=\"$(subst $(SPACE),_,$(USER_CFLAGS))\"
USER_DEFINES += USER_CPPFLAGS=\"$(subst $(SPACE),_,$(USER_CPPFLAGS))\"
USER_DEFINES += USER_ASMFLAGS=\"$(subst $(SPACE),_,$(USER_ASMFLAGS))\"
USER_DEFINES += USER_LDFLAGS=\"$(subst $(SPACE),_,$(USER_LDFLAGS))\"

HOST_DEFINES += HOST_COMPILEFLAGS=\"$(subst $(SPACE),_,$(HOST_COMPILEFLAGS))\"
HOST_DEFINES += HOST_CFLAGS=\"$(subst $(SPACE),_,$(HOST_CFLAGS))\"
HOST_DEFINES += HOST_CPPFLAGS=\"$(subst $(SPACE),_,$(HOST_CPPFLAGS))\"
HOST_DEFINES += HOST_ASMFLAGS=\"$(subst $(SPACE),_,$(HOST_ASMFLAGS))\"
HOST_DEFINES += HOST_LDFLAGS=\"$(subst $(SPACE),_,$(HOST_LDFLAGS))\"

#$(info LIBGCC = $(LIBGCC))
#$(info GLOBAL_COMPILEFLAGS = $(GLOBAL_COMPILEFLAGS))
#$(info GLOBAL_OPTFLAGS = $(GLOBAL_OPTFLAGS))

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
	rm -f $(KERNEL_ZBI) $(KERNEL_IMAGE) $(KERNEL_ELF) $(KERNEL_ELF).lst $(KERNEL_ELF).debug.lst $(KERNEL_ELF).sym $(KERNEL_ELF).sym.sorted $(KERNEL_ELF).size $(KERNEL_ELF).hex $(KERNEL_ELF).dump $(KERNEL_ELF)-gdb.py
	rm -f $(foreach app,$(ALLUSER_APPS),$(app) $(app).lst $(app).dump $(app).strip)

install: all
	scp $(KERNEL_ZBI) 192.168.0.4:/tftproot

# generate a config-global.h file with all of the GLOBAL_DEFINES laid out in #define format
$(GLOBAL_CONFIG_HEADER): FORCE
	@$(call MAKECONFIGHEADER,$@,GLOBAL_DEFINES,"#define __Fuchsia__ 1")

# generate a config-kernel.h file with all of the KERNEL_DEFINES laid out in #define format
$(KERNEL_CONFIG_HEADER): FORCE
	@$(call MAKECONFIGHEADER,$@,KERNEL_DEFINES,"")

# generate a config-user.h file with all of the USER_DEFINES laid out in #define format
$(USER_CONFIG_HEADER): FORCE
	@$(call MAKECONFIGHEADER,$@,USER_DEFINES,"")

$(HOST_CONFIG_HEADER): FORCE
	@$(call MAKECONFIGHEADER,$@,HOST_DEFINES,"")

GENERATED += $(GLOBAL_CONFIG_HEADER) $(KERNEL_CONFIG_HEADER) $(USER_CONFIG_HEADER) $(HOST_CONFIG_HEADER)

# Empty rule for the .d files. The above rules will build .d files as a side
# effect. Only works on gcc 3.x and above, however.
%.d:

ifeq ($(filter $(MAKECMDGOALS), clean), )
-include $(DEPS)
endif

endif

endif # make spotless
