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
    $(LOCAL_DIR)/zircon.cpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/hypervisor \
    system/ulib/zircon \
    system/ulib/fdio \

MODULE_STATIC_LIBS := \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/virtio \

include make/module.mk
