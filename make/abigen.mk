# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

SYSCALLS_SRC := system/public/zircon/syscalls.abigen

GEN_DIR := $(BUILDDIR)/gen

STAMPY := $(GEN_DIR)/abigen-stamp
# This gets STAMPY built (and generation complete) early in the build.
TARGET_MODDEPS += $(STAMPY)

AG_ZIRCON := $(GENERATED_INCLUDES)/zircon

AG_KERNEL_HEADER := $(AG_ZIRCON)/syscall-definitions.h
AG_KERNEL_TRACE := $(AG_ZIRCON)/syscall-ktrace-info.inc
AG_KERNEL_CATEGORY := $(AG_ZIRCON)/syscall-category.inc
AG_KERNEL_WRAPPERS := $(AG_ZIRCON)/syscall-kernel-wrappers.inc
AG_KERNEL_BRANCHES := $(AG_ZIRCON)/syscall-kernel-branches.S

AG_ULIB_VDSO_HEADER := $(AG_ZIRCON)/syscall-vdso-definitions.h
AG_ULIB_VDSO_WRAPPERS := $(AG_ZIRCON)/syscall-vdso-wrappers.inc
AG_ULIB_SYSCALL_NUMBER := $(AG_ZIRCON)/zx-syscall-numbers.h
AG_ULIB_ARM := $(AG_ZIRCON)/syscalls-arm64.S
AG_ULIB_X86 := $(AG_ZIRCON)/syscalls-x86-64.S

AG_SYSCALLS := $(AG_ZIRCON)/syscalls
AG_PUBLIC_HEADER := $(AG_SYSCALLS)/definitions.h
AG_PUBLIC_RUST := $(AG_SYSCALLS)/definitions.rs

AG_SYSROOT_ZIRCON := $(BUILDSYSROOT)/include/zircon
AG_SYSROOT_HEADER := $(AG_SYSROOT_ZIRCON)/syscalls/definitions.h
AG_SYSROOT_RUST := $(AG_SYSROOT_ZIRCON)/syscalls/definitions.rs

# STAMPY ultimately generates most of the files and paths here.
$(STAMPY): $(ABIGEN) $(SYSCALLS_SRC)
	$(call BUILDECHO,generating syscall files from $(SYSCALLS_SRC))
	$(NOECHO) mkdir -p $(AG_SYSCALLS)
	$(NOECHO) $(ABIGEN) \
		-trace $(AG_KERNEL_TRACE) \
		-category $(AG_KERNEL_CATEGORY) \
		-kernel-header $(AG_KERNEL_HEADER) \
		-kernel-wrappers $(AG_KERNEL_WRAPPERS) \
		-kernel-branch $(AG_KERNEL_BRANCHES) \
		-arm-asm $(AG_ULIB_ARM) \
		-x86-asm $(AG_ULIB_X86) \
		-vdso-header $(AG_ULIB_VDSO_HEADER) \
		-vdso-wrappers $(AG_ULIB_VDSO_WRAPPERS) \
		-numbers $(AG_ULIB_SYSCALL_NUMBER) \
		-user-header $(AG_PUBLIC_HEADER) \
		-rust $(AG_PUBLIC_RUST) \
		$(SYSCALLS_SRC)
	$(NOECHO) touch $(STAMPY)

run-abigen $(AG_PUBLIC_HEADER) $(AG_PUBLIC_RUST) $(AG_SYSROOT_HEADER) $(AG_SYSROOT_RUST): $(STAMPY)

GENERATED += $(AG_KERNEL_HEADER) $(AG_KERNEL_TRACE) \
	$(AG_KERNEL_CATEGORY) $(AG_ULIB_X86) $(AG_ULIB_ARM) \
	$(AG_KERNEL_WRAPPERS) $(AG_KERNEL_BRANCHES) \
	$(AG_ULIB_SYSCALL_NUMBERS) \
	$(AG_ULIB_VDSO_HEADER) $(AG_ULIB_VDSO_WRAPPERS) \
	$(AG_PUBLIC_HEADER) $(AG_SYSROOT_HEADER) \
	$(AG_PUBLIC_RUST) $(AG_SYSROOT_RUST) \
	$(STAMPY)

$(call copy-dst-src,$(AG_SYSROOT_HEADER),$(AG_PUBLIC_HEADER))
$(call copy-dst-src,$(AG_SYSROOT_RUST),$(AG_PUBLIC_RUST))

# needed to create c.pkg (see: module-userlib.mk)
ABIGEN_BUILDDIR := $(GENERATED_INCLUDES)
ABIGEN_PUBLIC_HEADERS := $(AG_PUBLIC_HEADER) $(AG_PUBLIC_RUST)

SYSROOT_DEPS += $(AG_SYSROOT_HEADER) $(AG_SYSROOT_RUST)
