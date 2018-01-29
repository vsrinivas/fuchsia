# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_EXPORT := a

MODULE_TYPE := userlib

MODULE_SRCS := \
    $(LOCAL_DIR)/guest.cpp \
    $(LOCAL_DIR)/phys_mem.cpp \
    $(LOCAL_DIR)/vcpu.cpp \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon \

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async.loop-cpp \
    system/ulib/async.loop \
    system/ulib/block-client \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp \

include system/ulib/hypervisor/arch/$(ARCH)/rules.mk
include make/module.mk
