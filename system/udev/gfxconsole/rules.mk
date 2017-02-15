# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/vc-device.cpp \
    $(LOCAL_DIR)/vc-gfx.cpp \
    $(LOCAL_DIR)/textcon.cpp \
    $(LOCAL_DIR)/main.cpp \

MODULE_STATIC_LIBS := ulib/ddk ulib/gfx ulib/hid ulib/mxcpp

MODULE_LIBS := ulib/driver ulib/mxio ulib/magenta ulib/musl

include make/module.mk
