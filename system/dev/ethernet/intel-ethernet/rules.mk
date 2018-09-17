# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ifeq ($(ARCH),x86)

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := $(LOCAL_DIR)/ethernet.c $(LOCAL_DIR)/ie.c

MODULE_STATIC_LIBS := system/ulib/ddk

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-ethernet \
    system/banjo/ddk-protocol-pci \

include make/module.mk

endif
