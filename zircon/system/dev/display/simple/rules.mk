# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

LOCAL_SRCS := $(LOCAL_DIR)/simple-display.cpp \

LOCAL_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/hwreg \
    system/ulib/zx \
    system/ulib/zxcpp \

LOCAL_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

# bochs driver

MODULE := $(LOCAL_DIR).bochs

MODULE_TYPE := driver

MODULE_SRCS := $(LOCAL_SRCS) $(LOCAL_DIR)/simple-bochs.c

MODULE_STATIC_LIBS := $(LOCAL_STATIC_LIBS)

MODULE_LIBS := $(LOCAL_LIBS)

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-display-controller \
    system/banjo/ddk-protocol-pci \

include make/module.mk

# amd-kaveri driver

MODULE := $(LOCAL_DIR).amd-kaveri

MODULE_TYPE := driver

MODULE_SRCS := $(LOCAL_SRCS) $(LOCAL_DIR)/simple-amd-kaveri.c

MODULE_STATIC_LIBS := $(LOCAL_STATIC_LIBS)

MODULE_LIBS := $(LOCAL_LIBS)

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-display-controller \
    system/banjo/ddk-protocol-pci \

include make/module.mk

# nv driver

MODULE := $(LOCAL_DIR).nv

MODULE_TYPE := driver

MODULE_SRCS := $(LOCAL_SRCS) $(LOCAL_DIR)/simple-nv.c

MODULE_STATIC_LIBS := $(LOCAL_STATIC_LIBS)

MODULE_LIBS := $(LOCAL_LIBS)

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-display-controller \
    system/banjo/ddk-protocol-pci \

include make/module.mk

# intel driver

MODULE := $(LOCAL_DIR).intel

MODULE_TYPE := driver

MODULE_SRCS := $(LOCAL_SRCS) $(LOCAL_DIR)/simple-intel.c

MODULE_STATIC_LIBS := $(LOCAL_STATIC_LIBS)

MODULE_LIBS := $(LOCAL_LIBS)

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-display-controller \
    system/banjo/ddk-protocol-pci \

include make/module.mk
