# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# If set, disable building these tests which account for a fair amount
# of build time
ifeq ($(call TOBOOL,$(DISABLE_FBL_TEST)),false)

LOCAL_DIR := $(GET_LOCAL_DIR)

fbl_common_tests := \
    $(LOCAL_DIR)/algorithm_tests.cpp \
    $(LOCAL_DIR)/array_tests.cpp \
    $(LOCAL_DIR)/atomic_tests.cpp \
    $(LOCAL_DIR)/auto_call_tests.cpp \
    $(LOCAL_DIR)/forward_tests.cpp \
    $(LOCAL_DIR)/function_tests.cpp \
    $(LOCAL_DIR)/initializer_list_tests.cpp \
    $(LOCAL_DIR)/intrusive_container_tests.cpp \
    $(LOCAL_DIR)/intrusive_doubly_linked_list_tests.cpp \
    $(LOCAL_DIR)/intrusive_hash_table_dll_tests.cpp \
    $(LOCAL_DIR)/intrusive_hash_table_sll_tests.cpp \
    $(LOCAL_DIR)/intrusive_singly_linked_list_tests.cpp \
    $(LOCAL_DIR)/intrusive_wavl_tree_tests.cpp \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/recycler_tests.cpp \
    $(LOCAL_DIR)/ref_ptr_tests.cpp \
    $(LOCAL_DIR)/string_buffer_tests.cpp \
    $(LOCAL_DIR)/string_piece_tests.cpp \
    $(LOCAL_DIR)/string_printf_tests.cpp \
    $(LOCAL_DIR)/string_tests.cpp \
    $(LOCAL_DIR)/string_traits_tests.cpp \
    $(LOCAL_DIR)/type_support_tests.cpp \
    $(LOCAL_DIR)/unique_free_ptr_tests.cpp \
    $(LOCAL_DIR)/unique_ptr_tests.cpp \
    $(LOCAL_DIR)/unique_fd_tests.cpp \
    $(LOCAL_DIR)/vector_tests.cpp \

fbl_device_tests := $(fbl_common_tests)

# These tests won't run on the host.
#
# Some of these tests need fbl::Mutex which currently isn't supported on the
# host. TODO(ZX-1053): Support fbl::Mutex on the host and make the ref counted
# and slab allocator tests work.
fbl_device_tests += \
    $(LOCAL_DIR)/memory_probe_tests.cpp \
    $(LOCAL_DIR)/ref_counted_tests.cpp \
    $(LOCAL_DIR)/slab_allocator_tests.cpp \

# These tests need zircon VMO and VMARs which are not currently supported on the
# host.
fbl_device_tests += \
    $(LOCAL_DIR)/vmo_vmar_tests.cpp \

fbl_host_tests := $(fbl_common_tests)

# Userspace tests.

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_NAME := fbl-test

MODULE_SRCS := $(fbl_device_tests)

MODULE_STATIC_LIBS := \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/zx \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk

# Host tests.

MODULE := $(LOCAL_DIR).hostapp

MODULE_TYPE := hostapp

MODULE_NAME := fbl-test

MODULE_SRCS := $(fbl_host_tests)

MODULE_COMPILEFLAGS := \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/unittest/include \

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \
    system/ulib/pretty.hostlib \
    system/ulib/unittest.hostlib \

include make/module.mk

# Clear local variables.

fbl_common_tests :=
fbl_device_tests :=
fbl_host_tests :=

endif # DISABLE_FBL_TEST
