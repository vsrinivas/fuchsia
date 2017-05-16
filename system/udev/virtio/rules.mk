# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/block.cpp \
    $(LOCAL_DIR)/device.cpp \
    $(LOCAL_DIR)/gpu.cpp \
    $(LOCAL_DIR)/ring.cpp \
    $(LOCAL_DIR)/utils.cpp \
    $(LOCAL_DIR)/virtio_c.c \
    $(LOCAL_DIR)/virtio_driver.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/pretty \
    system/ulib/mx \
    system/ulib/mxalloc \
    system/ulib/mxcpp \
    system/ulib/mxtl \

MODULE_LIBS := system/ulib/driver system/ulib/magenta system/ulib/c

include make/module.mk
