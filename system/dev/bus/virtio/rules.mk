# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_INCLUDES := $(LOCAL_DIR)
MODULE_SRCS := \
    $(LOCAL_DIR)/block.cpp \
    $(LOCAL_DIR)/console.cpp \
    $(LOCAL_DIR)/device.cpp \
    $(LOCAL_DIR)/ethernet.cpp \
    $(LOCAL_DIR)/gpu.cpp \
    $(LOCAL_DIR)/input.cpp \
    $(LOCAL_DIR)/ring.cpp \
    $(LOCAL_DIR)/rng.cpp \
    $(LOCAL_DIR)/socket.cpp \
    $(LOCAL_DIR)/virtio_c.c \
    $(LOCAL_DIR)/virtio_driver.cpp \
	$(LOCAL_DIR)/backends/pci.cpp \
	$(LOCAL_DIR)/backends/pci_legacy.cpp \
	$(LOCAL_DIR)/backends/pci_modern.cpp \

MODULE_FIDL_LIBS := system/fidl/fuchsia-hardware-vsock

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/hid \
    system/ulib/hwreg \
    system/ulib/pretty \
    system/ulib/sync \
    system/ulib/virtio \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-block \
    system/banjo/ddk-protocol-display-controller \
    system/banjo/ddk-protocol-ethernet \
    system/banjo/ddk-protocol-hidbus \
    system/banjo/ddk-protocol-pci \

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

include make/module.mk
