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
    $(LOCAL_DIR)/virtio_c.c \
    $(LOCAL_DIR)/virtio_driver.cpp \
	$(LOCAL_DIR)/backends/pci.cpp \
	$(LOCAL_DIR)/backends/pci_legacy.cpp \
	$(LOCAL_DIR)/backends/pci_modern.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/pretty \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/virtio \
    system/ulib/sync \

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

include make/module.mk
