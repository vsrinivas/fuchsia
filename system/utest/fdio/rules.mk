# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/fdio_handle_fd.c \
    $(LOCAL_DIR)/fdio_root.c \
    $(LOCAL_DIR)/fdio_path_canonicalize.c \
    $(LOCAL_DIR)/fdio_socketpair.c

MODULE_NAME := fdio-test

MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \

include make/module.mk
