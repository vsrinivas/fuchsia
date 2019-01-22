# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/ramdisk.cpp \
    $(LOCAL_DIR)/ramdisk-controller.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/operation \
    system/ulib/async \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/fidl-async \
    system/ulib/fidl-utils \
    system/ulib/fzl \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-block \
    system/banjo/ddk-protocol-block-partition \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-hardware-ramdisk \
    system/fidl/fuchsia-io \
    system/fidl/fuchsia-mem \

include make/module.mk
