# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

ifeq ($(call TOBOOL, $(ENABLE_USER_PCI)), true)
$(warning Building with userspace pci!)

MODULE := $(LOCAL_DIR)
MODULE_TYPE := driver
MODULE_NAME := bus-pci
MODULE_SRCS := \
    $(LOCAL_DIR)/bus.cpp \
    $(LOCAL_DIR)/config.cpp \
    $(LOCAL_DIR)/root.cpp \
    $(LOCAL_DIR)/upstream_node.cpp

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-pci \
    system/banjo/ddk-protocol-pciroot

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/pretty \
    system/ulib/region-alloc \
    system/ulib/zx \
    system/ulib/zxcpp

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c

include make/module.mk

else
MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver
MODULE_NAME := bus-pci
MODULE_SRCS := $(LOCAL_DIR)/kpci/kpci.c
MODULE_STATIC_LIBS := system/ulib/ddk
MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-pci \
    system/banjo/ddk-protocol-pciroot \
    system/banjo/ddk-protocol-platform-device
include make/module.mk

MODULE := $(LOCAL_DIR).proxy
MODULE_TYPE := driver
MODULE_NAME := bus-pci.proxy
MODULE_SRCS := $(LOCAL_DIR)/kpci/proxy.c
MODULE_STATIC_LIBS := system/ulib/ddk
MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-pci \
    system/banjo/ddk-protocol-pciroot \
    system/banjo/ddk-protocol-platform-device
include make/module.mk
endif
