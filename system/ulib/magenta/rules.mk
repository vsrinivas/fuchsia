# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

# This library should not depend on libc.
MODULE_COMPILEFLAGS := -ffreestanding

MODULE_SRCS = \
    $(LOCAL_DIR)/mx_status_get_string.c

ifeq ($(ARCH),arm)
MODULE_SRCS += $(LOCAL_DIR)/syscalls-arm32.S
else ifeq ($(ARCH),arm64)
MODULE_SRCS += $(LOCAL_DIR)/syscalls-arm64.S
else ifeq ($(ARCH),x86)
    ifeq ($(SUBARCH),x86-64)
    MODULE_SRCS += $(LOCAL_DIR)/syscalls-x86-64.S
    else
    MODULE_SRCS += $(LOCAL_DIR)/syscalls-x86.S
    endif
endif

# This gets an ABI stub installed in sysroots, but the DSO never gets
# installed on disk because it's delivered magically by the kernel.
MODULE_SO_NAME := magenta
MODULE_SO_INSTALL_NAME := -

# All the code this DSO is pure read-only/reentrant code that
# does not need any writable data (except its caller's stack).
# Make it use a simplified, hardened memory layout.
MODULE_LDFLAGS := -T $(BUILDDIR)/rodso.ld
MODULE_EXTRA_OBJS := $(BUILDDIR)/rodso-stamp

include make/module.mk
