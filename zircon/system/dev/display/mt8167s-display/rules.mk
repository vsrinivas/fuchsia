# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/mt8167s-display.cpp \
    $(LOCAL_DIR)/ovl.cpp \
    $(LOCAL_DIR)/disp-rdma.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/mmio \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/hwreg \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-display-controller \
    system/banjo/ddk-protocol-platform-device \
    system/banjo/ddk-protocol-sysmem \

MODULE_FIDL_LIBS := system/fidl/fuchsia-sysmem

include make/module.mk
