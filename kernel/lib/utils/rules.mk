# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

KERNEL_INCLUDES += $(LOCAL_DIR)/include

MODULE_SRCS := \
    $(LOCAL_DIR)/arena.cpp \
    $(LOCAL_DIR)/arena_tests.cpp \
    $(LOCAL_DIR)/auto_call_tests.cpp \
    $(LOCAL_DIR)/fifo_buffer_tests.cpp \
    $(LOCAL_DIR)/forward_tests.cpp \
    $(LOCAL_DIR)/hash_tests.cpp \
    $(LOCAL_DIR)/intrusive_container_tests.cpp \
    $(LOCAL_DIR)/intrusive_doubly_linked_list_tests.cpp \
    $(LOCAL_DIR)/intrusive_singly_linked_list_tests.cpp \
    $(LOCAL_DIR)/ref_counted_tests.cpp \
    $(LOCAL_DIR)/ref_ptr_tests.cpp \
    $(LOCAL_DIR)/unique_ptr_tests.cpp \

include make/module.mk

