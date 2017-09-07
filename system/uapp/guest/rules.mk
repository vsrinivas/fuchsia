# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := misc

MODULE_SRCS += \
    $(LOCAL_DIR)/guest.cpp \
    $(LOCAL_DIR)/linux.cpp \
    $(LOCAL_DIR)/magenta.cpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/hypervisor \
    system/ulib/magenta \
    system/ulib/mxio \

MODULE_STATIC_LIBS := \
    system/ulib/mxcpp \
    system/ulib/fbl \
    system/ulib/virtio \

include make/module.mk
