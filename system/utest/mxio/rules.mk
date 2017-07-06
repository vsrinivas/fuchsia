# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/mxio_handle_fd.c \
    $(LOCAL_DIR)/mxio_root.c \
    $(LOCAL_DIR)/mxio_path_canonicalize.c \
    $(LOCAL_DIR)/mxio_socketpair.c

MODULE_NAME := mxio-test

MODULE_LIBS := \
    system/ulib/magenta \
    system/ulib/c \
    system/ulib/mxio \
    system/ulib/unittest \

include make/module.mk
