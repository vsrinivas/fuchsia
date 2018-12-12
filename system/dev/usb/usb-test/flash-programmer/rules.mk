# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/flash-programmer.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/usb \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := system/ulib/driver system/ulib/c system/ulib/zircon

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-mem \
    system/fidl/zircon-usb-test-fwloader \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-usb-composite \
    system/banjo/ddk-protocol-usb-request \

ifeq ($(call TOBOOL,$(INTERNAL_ACCESS)),true)
MODULE_FIRMWARE := fx3-boot/Fx3BootAppGcc.img
endif

include make/module.mk
