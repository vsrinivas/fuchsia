# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
    $(LOCAL_DIR)/benchmarks.c \
    $(LOCAL_DIR)/cache_tests.c \
    $(LOCAL_DIR)/clock_tests.c \
    $(LOCAL_DIR)/fibo.c \
    $(LOCAL_DIR)/float.c \
    $(LOCAL_DIR)/float_instructions.S \
    $(LOCAL_DIR)/float_test_vec.c \
    $(LOCAL_DIR)/mem_tests.c \
    $(LOCAL_DIR)/port_tests.c \
    $(LOCAL_DIR)/printf_tests.c \
    $(LOCAL_DIR)/ref_call_counter.cpp \
    $(LOCAL_DIR)/ref_ptr_tests.cpp \
    $(LOCAL_DIR)/sync_ipi_tests.c \
    $(LOCAL_DIR)/tests.c \
    $(LOCAL_DIR)/thread_tests.c \
    $(LOCAL_DIR)/unique_ptr_tests.cpp \


MODULE_DEPS += \
    lib/safeint \
    lib/unittest \
    lib/utils \
    lib/crypto \

MODULE_COMPILEFLAGS += -Wno-format -fno-builtin

include make/module.mk
