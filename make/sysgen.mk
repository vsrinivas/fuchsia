# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

SYSCALLS_SRC := system/public/magenta/syscalls.sysgen

GEN_DIR := $(BUILDDIR)/gen

STAMPY := $(GEN_DIR)/sysgen-stamp
# This gets STAMPY built (and generation complete) early in the build.
TARGET_MODDEPS += $(STAMPY)

SG_MAGENTA := $(GENERATED_INCLUDES)/magenta

SG_KERNEL_CODE := $(SG_MAGENTA)/syscall-invocation-cases.inc
SG_KERNEL_HEADER := $(SG_MAGENTA)/syscall-definitions.h
SG_KERNEL_TRACE := $(SG_MAGENTA)/syscall-ktrace-info.inc
SG_KERNEL_CATEGORY := $(SG_MAGENTA)/syscall-category.inc
SG_KERNEL_WRAPPERS := $(SG_MAGENTA)/syscall-kernel-wrappers.inc
SG_KERNEL_BRANCHES := $(SG_MAGENTA)/syscall-kernel-branches.S

SG_ULIB_VDSO_HEADER := $(SG_MAGENTA)/syscall-vdso-definitions.h
SG_ULIB_VDSO_WRAPPERS := $(SG_MAGENTA)/syscall-vdso-wrappers.inc
SG_ULIB_SYSCALL_NUMBER := $(SG_MAGENTA)/mx-syscall-numbers.h
SG_ULIB_ARM := $(SG_MAGENTA)/syscalls-arm64.S
SG_ULIB_X86 := $(SG_MAGENTA)/syscalls-x86-64.S

SG_SYSCALLS := $(SG_MAGENTA)/syscalls
SG_PUBLIC_HEADER := $(SG_SYSCALLS)/definitions.h
SG_PUBLIC_RUST := $(SG_SYSCALLS)/definitions.rs

SG_SYSROOT_MAGENTA := $(BUILDSYSROOT)/include/magenta
SG_SYSROOT_HEADER := $(SG_SYSROOT_MAGENTA)/syscalls/definitions.h
SG_SYSROOT_RUST := $(SG_SYSROOT_MAGENTA)/syscalls/definitions.rs

# STAMPY ultimately generates most of the files and paths here.
$(STAMPY): $(SYSGEN) $(SYSCALLS_SRC)
	$(call BUILDECHO,generating syscall files from $(SYSCALLS_SRC))
	$(NOECHO) mkdir -p $(SG_SYSCALLS)
	$(NOECHO) $(SYSGEN) \
		-kernel-code $(SG_KERNEL_CODE) \
		-trace $(SG_KERNEL_TRACE) \
		-category $(SG_KERNEL_CATEGORY) \
		-kernel-header $(SG_KERNEL_HEADER) \
		-kernel-wrappers $(SG_KERNEL_WRAPPERS) \
		-kernel-branch $(SG_KERNEL_BRANCHES) \
		-arm-asm $(SG_ULIB_ARM) \
		-x86-asm $(SG_ULIB_X86) \
		-vdso-header $(SG_ULIB_VDSO_HEADER) \
		-vdso-wrappers $(SG_ULIB_VDSO_WRAPPERS) \
		-numbers $(SG_ULIB_SYSCALL_NUMBER) \
		-user-header $(SG_PUBLIC_HEADER) \
		-rust $(SG_PUBLIC_RUST) \
		$(SYSCALLS_SRC)
	$(NOECHO) touch $(STAMPY)

run-sysgen $(SG_PUBLIC_HEADER) $(SG_PUBLIC_RUST) $(SG_SYSROOT_HEADER) $(SG_SYSROOT_RUST): $(STAMPY)

GENERATED += $(SG_KERNEL_CODE) $(SG_KERNEL_HEADER) $(SG_KERNEL_TRACE) \
	$(SG_KERNEL_CATEGORY) $(SG_ULIB_X86) $(SG_ULIB_ARM) \
	$(SG_KERNEL_WRAPPERS) $(SG_KERNEL_BRANCHES) \
	$(SG_ULIB_SYSCALL_NUMBERS) \
	$(SG_ULIB_VDSO_HEADER) $(SG_ULIB_VDSO_WRAPPERS) \
	$(SG_PUBLIC_HEADER) $(SG_SYSROOT_HEADER) \
	$(SG_PUBLIC_RUST) $(SG_SYSROOT_RUST) \
	$(STAMPY)

$(call copy-dst-src,$(SG_SYSROOT_HEADER),$(SG_PUBLIC_HEADER))
$(call copy-dst-src,$(SG_SYSROOT_RUST),$(SG_PUBLIC_RUST))

SYSROOT_DEPS += $(SG_SYSROOT_HEADER) $(SG_SYSROOT_RUST)
