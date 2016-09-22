# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS := \
    lib/console \
    lib/crypto \
    lib/magenta \
    lib/user_copy \

MODULE_SRCS := \
    $(LOCAL_DIR)/syscalls.cpp \
    $(LOCAL_DIR)/syscalls_datapipe.cpp \
    $(LOCAL_DIR)/syscalls_debug.cpp \
    $(LOCAL_DIR)/syscalls_ddk.cpp \
    $(LOCAL_DIR)/syscalls_exceptions.cpp \
    $(LOCAL_DIR)/syscalls_magenta.cpp \
    $(LOCAL_DIR)/syscalls_msgpipe.cpp \
    $(LOCAL_DIR)/syscalls_test.cpp \
    $(LOCAL_DIR)/syscalls_handle_ops.cpp \
    $(LOCAL_DIR)/syscalls_handle_wait.cpp \
    $(LOCAL_DIR)/syscalls_vmo.cpp \

include make/module.mk
