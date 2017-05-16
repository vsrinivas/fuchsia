# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

#MODULE_SRCS += \

MODULE_SRCS += \
    $(LOCAL_DIR)/algorithm_tests.cpp \
    $(LOCAL_DIR)/array_tests.cpp \
    $(LOCAL_DIR)/atomic_tests.cpp \
    $(LOCAL_DIR)/auto_call_tests.cpp \
    $(LOCAL_DIR)/forward_tests.cpp \
    $(LOCAL_DIR)/intrusive_container_tests.cpp \
    $(LOCAL_DIR)/intrusive_doubly_linked_list_tests.cpp \
    $(LOCAL_DIR)/intrusive_hash_table_dll_tests.cpp \
    $(LOCAL_DIR)/intrusive_hash_table_sll_tests.cpp \
    $(LOCAL_DIR)/intrusive_singly_linked_list_tests.cpp \
    $(LOCAL_DIR)/intrusive_wavl_tree_tests.cpp \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/recycler_tests.cpp \
    $(LOCAL_DIR)/ref_counted_tests.cpp \
    $(LOCAL_DIR)/ref_ptr_tests.cpp \
    $(LOCAL_DIR)/slab_allocator_tests.cpp \
    $(LOCAL_DIR)/string_piece_tests.cpp \
    $(LOCAL_DIR)/type_support_tests.cpp \
    $(LOCAL_DIR)/unique_free_ptr_tests.cpp \
    $(LOCAL_DIR)/unique_ptr_tests.cpp \

MODULE_NAME := mxtl-test

MODULE_STATIC_LIBS := \
    system/ulib/mxalloc \
    system/ulib/mxcpp \
    system/ulib/mxtl \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/mxio \
    system/ulib/unittest \

include make/module.mk
