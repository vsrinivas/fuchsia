# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

# This library should not depend on libc.
MODULE_COMPILEFLAGS := -ffreestanding $(NO_SAFESTACK) $(NO_SANITIZERS)

MODULE_HEADER_DEPS := kernel/lib/vdso

MODULE_SRCS := \
    $(LOCAL_DIR)/data.S \
    $(LOCAL_DIR)/zx_cache_flush.cpp \
    $(LOCAL_DIR)/zx_channel_call.cpp \
    $(LOCAL_DIR)/zx_cprng_draw.cpp \
    $(LOCAL_DIR)/zx_deadline_after.cpp \
    $(LOCAL_DIR)/zx_status_get_string.cpp \
    $(LOCAL_DIR)/zx_system_get_dcache_line_size.cpp \
    $(LOCAL_DIR)/zx_system_get_features.cpp \
    $(LOCAL_DIR)/zx_system_get_num_cpus.cpp \
    $(LOCAL_DIR)/zx_system_get_physmem.cpp \
    $(LOCAL_DIR)/zx_system_get_version.cpp \
    $(LOCAL_DIR)/zx_ticks_get.cpp \
    $(LOCAL_DIR)/zx_ticks_per_second.cpp \
    $(LOCAL_DIR)/syscall-wrappers.cpp \

ifeq ($(ARCH),arm64)
MODULE_SRCS += \
    $(LOCAL_DIR)/zx_futex_wake_handle_close_thread_exit-arm64.S \
    $(LOCAL_DIR)/zx_vmar_unmap_handle_close_thread_exit-arm64.S \
    $(LOCAL_DIR)/syscalls-arm64.S
else ifeq ($(ARCH),x86)
MODULE_SRCS += \
    $(LOCAL_DIR)/zx_futex_wake_handle_close_thread_exit-x86-64.S \
    $(LOCAL_DIR)/zx_vmar_unmap_handle_close_thread_exit-x86-64.S \
    $(LOCAL_DIR)/syscalls-x86-64.S
endif

# This gets an ABI stub installed in sysroots, but the DSO never gets
# installed on disk because it's delivered magically by the kernel.
MODULE_EXPORT := so
MODULE_SO_NAME := zircon
MODULE_SO_INSTALL_NAME := -

# All the code this DSO is pure read-only/reentrant code that
# does not need any writable data (except its caller's stack).
# Make it use a simplified, hardened memory layout.
MODULE_LDFLAGS := $(RODSO_LDFLAGS)

include make/module.mk
