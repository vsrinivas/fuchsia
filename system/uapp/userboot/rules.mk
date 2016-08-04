# Copyright 2016 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/bootfs.c \
    $(LOCAL_DIR)/elf.c \
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
# If the compiler generates calls to memcpy or whatnot, it will use PLT
# entries for them, which we cannot allow.  So tell the compiler not to.
MODULE_COMPILEFLAGS += -ffreestanding

MODULE_STATIC_LIBS := ulib/elfload ulib/runtime ulib/ddk ulib/musl-static
MODULE_HEADER_LIBS := ulib/magenta

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
