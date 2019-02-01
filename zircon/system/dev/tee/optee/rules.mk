# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/binding.c \
    $(LOCAL_DIR)/optee-client.cpp \
    $(LOCAL_DIR)/optee-controller.cpp \
    $(LOCAL_DIR)/optee-message.cpp \
    $(LOCAL_DIR)/shared-memory.cpp \
    $(LOCAL_DIR)/util.cpp \

MODULE_FIDL_LIBS := system/fidl/fuchsia-hardware-tee

MODULE_STATIC_LIBS := \
    system/dev/lib/mmio \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/region-alloc \
    system/ulib/tee-client-api \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/driver \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-platform-device \

include make/module.mk
