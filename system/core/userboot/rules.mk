# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver
# not really a "driver", but a "driver" is a dso that's not
# exported via sysroot, which is what userboot is too

MODULE_SRCS += \
    $(LOCAL_DIR)/bootdata.c \
    $(LOCAL_DIR)/bootfs.c \
    $(LOCAL_DIR)/userboot-elf.c \
    $(LOCAL_DIR)/option.c \
    $(LOCAL_DIR)/start.c \
    $(LOCAL_DIR)/loader-service.c \
    $(LOCAL_DIR)/util.c

MODULE_NAME := userboot
MODULE_GROUP := core

# This is built as a shared library, but it gets embedded directly in the
# kernel image and does not need to be installed in the filesystem at all.
MODULE_SO_NAME := userboot
MODULE_SO_INSTALL_NAME := -

# Directly compile in the few functions we need from libc.
# This doesn't get arch-specific optimized versions, but
# such optimization isn't very important for userboot.
userboot-string-functions := memcmp memcpy memset strlen strncmp memmove
MODULE_SRCS += \
    $(userboot-string-functions:%=third_party/ulib/musl/src/string/%.c)
MODULE_COMPILEFLAGS += -Ithird_party/ulib/musl/src/internal

# Make sure there are never any PLT entries generated.
MODULE_COMPILEFLAGS += -fvisibility=hidden

ifeq ($(call TOBOOL,$(USE_LTO)),true)
# Make sure that compiler doesn't replace calls to libc functions with builtins.
# While inlining these builtins is desirable, it causes LTO to optimize away
# our own versions of these functions which later causes a link failure.
# TODO(phosek): https://bugs.llvm.org/show_bug.cgi?id=34169
MODULE_COMPILEFLAGS += -ffreestanding
endif

# We don't have normal setup, so safe-stack is a non-starter.
MODULE_COMPILEFLAGS += $(NO_SAFESTACK) $(NO_SANITIZERS)

# system/ulib/runtime is compiled without safe-stack.  We can't use any other
# static libs, because they might be built with safe-stack or other
# options that can't be supported in the constrained userboot context.
MODULE_STATIC_LIBS := system/ulib/runtime
MODULE_HEADER_DEPS := system/ulib/magenta

# Fortunately, each of these libraries is just a single source file.
# So we just use their sources directly rather than getting
# clever with the build system somehow.

MODULE_HEADER_DEPS += system/ulib/elfload
MODULE_SRCS += system/ulib/elfload/elf-load.c

MODULE_HEADER_DEPS += system/ulib/bootdata
MODULE_SRCS += system/ulib/bootdata/decompress.c

MODULE_HEADER_DEPS += third_party/ulib/lz4
MODULE_SRCS += third_party/ulib/lz4/lz4.c
MODULE_COMPILEFLAGS += -Ithird_party/ulib/lz4/include/lz4 -DWITH_LZ4_NOALLOC

# This generated header lists all the ABI symbols in the vDSO with their
# addresses.  It's used to generate vdso-syms.ld, below.
$(BUILDDIR)/$(LOCAL_DIR)/vdso-syms.h: $(BUILDDIR)/system/ulib/magenta/libmagenta.so
	@$(MKDIR)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(SHELLEXEC) scripts/shlib-symbols -a '$(NM)' $< > $@
GENERATED += $(BUILDDIR)/$(LOCAL_DIR)/vdso-syms.h

# This generated linker script defines symbols for each vDSO entry point
# giving the relative address where it will be found at runtime.  With
# this hack, the userboot code doesn't need to do any special work to
# find the vDSO and its entry points, keeping the code far simpler.
$(BUILDDIR)/$(LOCAL_DIR)/vdso-syms.ld: \
    $(LOCAL_DIR)/vdso-syms.ld.h $(BUILDDIR)/$(LOCAL_DIR)/vdso-syms.h
	@$(MKDIR)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(CC) -E -P -include $^ > $@
GENERATED += $(BUILDDIR)/$(LOCAL_DIR)/vdso-syms.ld.h
MODULE_EXTRA_OBJS := $(BUILDDIR)/$(LOCAL_DIR)/vdso-syms.ld

# userboot is a reentrant DSO (no writable segment) with an entry point.
MODULE_LDFLAGS := $(RODSO_LDFLAGS) -e _start

include make/module.mk
