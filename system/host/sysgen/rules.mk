# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := hostapp

MODULE_SRCS += \
    $(LOCAL_DIR)/generator.cpp \
    $(LOCAL_DIR)/header_generator.cpp \
    $(LOCAL_DIR)/kernel_invocation_generator.cpp \
    $(LOCAL_DIR)/kernel_wrapper_generator.cpp \
    $(LOCAL_DIR)/rust_binding_generator.cpp \
    $(LOCAL_DIR)/syscall_parser.cpp \
    $(LOCAL_DIR)/sysgen.cpp \
    $(LOCAL_DIR)/sysgen_generator.cpp \
    $(LOCAL_DIR)/types.cpp \
    $(LOCAL_DIR)/vdso_wrapper_generator.cpp \
    $(LOCAL_DIR)/parser/parser.cpp \

include make/module.mk
