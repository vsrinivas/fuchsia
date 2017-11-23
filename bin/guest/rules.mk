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

MODULE_HEADER_DEPS := \
    system/ulib/hid \
    system/ulib/virtio \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/machina \
    system/ulib/zircon \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/hypervisor \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_CPPFLAGS += \
    -Isystem/ulib/hypervisor/arch/$(ARCH)/include \
    -Isystem/ulib/machina/arch/$(ARCH)/include \

include make/module.mk
