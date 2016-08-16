# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp-static

MODULE_SRCS := \
    system/udev/intel-ethernet/ethernet.c \
    system/udev/intel-ethernet/ie.c

MODULE_NAME := test-driver

MODULE_STATIC_LIBS := \
    ulib/mxio ulib/magenta ulib/runtime ulib/ddk ulib/driver ulib/musl-static

include make/module.mk
