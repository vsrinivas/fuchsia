# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

SYSGEN_APP := $(BUILDDIR)/tools/sysgen
SYSCALLS_SRC := system/public/magenta/syscalls.sysgen

GEN_DIR := $(BUILDDIR)/gen

STAMPY := $(GEN_DIR)/sysgen-stamp
# This gets STAMPY built (and generation complete) early in the build.
GLOBAL_SRCDEPS += $(STAMPY)

SG_MAGENTA := $(GENERATED_INCLUDES)/magenta

SG_KERNEL_CODE := $(SG_MAGENTA)/syscall-invocation-cases.inc
SG_KERNEL_HEADER := $(SG_MAGENTA)/syscall-definitions.h
SG_KERNEL_TRACE := $(SG_MAGENTA)/syscall-ktrace-info.inc

SG_ULIB_VDSO_HEADER := $(SG_MAGENTA)/syscall-vdso-definitions.h
SG_ULIB_SYSCALL_NUMBER := $(SG_MAGENTA)/mx-syscall-numbers.h
SG_ULIB_ARM := $(SG_MAGENTA)/syscalls-arm64.S
SG_ULIB_X86 := $(SG_MAGENTA)/syscalls-x86-64.S

SG_SYSCALLS := $(SG_MAGENTA)/syscalls
SG_PUBLIC_HEADER := $(SG_SYSCALLS)/definitions.h

SG_SYSROOT_MAGENTA := $(BUILDDIR)/sysroot/include/magenta
SG_SYSROOT_HEADER := $(SG_SYSROOT_MAGENTA)/syscalls/definitions.h

# STAMPY ultimately generates most of the files and paths here.
$(STAMPY): $(SYSGEN_APP) $(SYSCALLS_SRC)
	$(info Generating syscall files from $(SYSCALLS_SRC) into $(SG_MAGENTA))
	$(NOECHO) mkdir -p $(SG_SYSCALLS)
	$(NOECHO) $(SYSGEN_APP) -t kernel-code   -f $(SG_KERNEL_CODE)          $(SYSCALLS_SRC)
	$(NOECHO) $(SYSGEN_APP) -t trace         -f $(SG_KERNEL_TRACE)         $(SYSCALLS_SRC)
	$(NOECHO) $(SYSGEN_APP) -t kernel-header -f $(SG_KERNEL_HEADER)        $(SYSCALLS_SRC)
	$(NOECHO) $(SYSGEN_APP) -t arm-asm       -f $(SG_ULIB_ARM)             $(SYSCALLS_SRC)
	$(NOECHO) $(SYSGEN_APP) -t x86-asm       -f $(SG_ULIB_X86)             $(SYSCALLS_SRC)
	$(NOECHO) $(SYSGEN_APP) -t vdso-header   -f $(SG_ULIB_VDSO_HEADER)     $(SYSCALLS_SRC)
	$(NOECHO) $(SYSGEN_APP) -t numbers       -f $(SG_ULIB_SYSCALL_NUMBER)  $(SYSCALLS_SRC)
	$(NOECHO) $(SYSGEN_APP) -t user-header   -f $(SG_PUBLIC_HEADER)        $(SYSCALLS_SRC)
	$(NOECHO) touch $(STAMPY)

run-sysgen $(SG_PUBLIC_HEADER) $(SG_SYSROOT_HEADER): $(STAMPY)

GENERATED += $(SG_KERNEL_CODE) $(SG_KERNEL_HEADER) $(SG_KERNEL_TRACE) $(SG_ULIB_X86) \
             $(SG_ULIB_ARM) $(SG_ULIB_SYSCALL_NUMBERS) $(SG_ULIB_VDSO_HEADER) \
             $(SG_PUBLIC_HEADER) $(SG_SYSROOT_HEADER) $(STAMPY)

$(call copy-dst-src,$(SG_SYSROOT_HEADER),$(SG_PUBLIC_HEADER))

ifeq ($(ENABLE_BUILD_SYSDEPS),true)
	$(call sysroot-file,$(SG_SYSROOT_HEADER),$(SG_PUBLIC_HEADER))
endif

SYSROOT_DEPS += $(SG_SYSROOT_HEADER)
