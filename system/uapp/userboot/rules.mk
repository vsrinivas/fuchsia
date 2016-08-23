# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/bootfs.c \
    $(LOCAL_DIR)/userboot-elf.c \
    $(LOCAL_DIR)/option.c \
    $(LOCAL_DIR)/start.c \
    $(LOCAL_DIR)/util.c

MODULE_NAME := userboot

# This is built as a shared library, but it gets embedded directly in the
# kernel image and does not need to be installed in the filesystem at all.
MODULE_SO_NAME := userboot
MODULE_SO_INSTALL_NAME := -

# We do link against musl-static to get the string functions.  But we
# carefully use hidden visibility for these so they have no PLT entries.
# The compiler inlines memcpy et al as builtins and then (often)
# generates calls to those same functions as an implementaiton detail of
# its builtins; those generated calls use PLT entries.  Using the
# -ffreestanding switch would tell the compiler not to inline memcpy et
# al, so the explicit calls in the source would respect the hidden
# visibility we used to declare them.  However, even with -ffreestanding
# the compiler can generate calls to memcpy, memset, etc. on its own.
# If the compiler generates a call to (e.g. memcpy) in a translation
# unit where there was no explicit call to that symbol, it won't emit
# the .hidden marker in the assembly for the symbol, meaning the linker
# will generate a PLT entry and relocation for it, which we cannot
# allow.  So don't bother with -ffreestanding, since it doesn't solve
# the problem anyway and some inlining might be beneficial.  Instead
# feed every translation unit a header file that stuffs some .hidden
# declarations into the assembly to cover symbols compilers might use.
MODULE_COMPILEFLAGS += -include $(LOCAL_DIR)/hidden.h

MODULE_STATIC_LIBS := ulib/elfload ulib/runtime ulib/ddk ulib/musl-static
MODULE_HEADER_DEPS := ulib/magenta

# This generated header lists all the ABI symbols in the vDSO with their
# addresses.  It's used to generate vdso-syms.ld, below.
$(BUILDDIR)/$(LOCAL_DIR)/vdso-syms.h: $(BUILDDIR)/ulib/magenta/libmagenta.so
	@$(MKDIR)
	@echo generating $@
	$(NOECHO)scripts/shlib-symbols -a '$(NM)' $< > $@
GENERATED += $(BUILDDIR)/$(LOCAL_DIR)/vdso-syms.h

# This generated linker script defines symbols for each vDSO entry point
# giving the relative address where it will be found at runtime.  With
# this hack, the userboot code doesn't need to do any special work to
# find the vDSO and its entry points, keeping the code far simpler.
$(BUILDDIR)/$(LOCAL_DIR)/vdso-syms.ld: \
    $(LOCAL_DIR)/vdso-syms.ld.h $(BUILDDIR)/$(LOCAL_DIR)/vdso-syms.h
	@$(MKDIR)
	@echo generating $@
	$(NOECHO)$(CC) -E -P -include $^ > $@
GENERATED += $(BUILDDIR)/$(LOCAL_DIR)/vdso-syms.ld.h

# userboot is a reentrant DSO (no writable segment) with an entry point.
MODULE_LDFLAGS := -T $(BUILDDIR)/rodso.ld -e _start

MODULE_EXTRA_OBJS := \
    $(BUILDDIR)/$(LOCAL_DIR)/vdso-syms.ld \
    $(BUILDDIR)/rodso-stamp

include make/module.mk
