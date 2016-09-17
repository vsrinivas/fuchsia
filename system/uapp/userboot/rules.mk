# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver
# not really a "driver", but a "driver" is a dso that's not
# exported via sysroot, which is what userboot is too

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

# Directly compile in the few functions we need from libc.
# This doesn't get arch-specific optimized versions, but
# such optimization isn't very important for userboot.
userboot-string-functions := memcmp memcpy memset strlen strncmp
MODULE_SRCS += \
    $(userboot-string-functions:%=third_party/ulib/musl/src/string/%.c)

# Make sure there are never any PLT entries generated.
MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_STATIC_LIBS := ulib/elfload ulib/runtime
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
