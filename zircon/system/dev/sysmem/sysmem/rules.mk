# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

#
# Build sysmem implementation driver.
#

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/allocator.cpp \
    $(LOCAL_DIR)/amlogic_memory_allocator.cpp \
    $(LOCAL_DIR)/binding.cpp \
    $(LOCAL_DIR)/buffer_collection.cpp \
    $(LOCAL_DIR)/buffer_collection_token.cpp \
    $(LOCAL_DIR)/device.cpp \
    $(LOCAL_DIR)/driver.cpp \
    $(LOCAL_DIR)/koid_util.cpp \
    $(LOCAL_DIR)/logging.cpp \
    $(LOCAL_DIR)/logical_buffer_collection.cpp \
    $(LOCAL_DIR)/usage_pixel_format_cost.cpp \

MODULE_FIDL_LIBS := system/fidl/fuchsia-sysmem

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async-loop.cpp \
    system/ulib/async-loop \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/fidl-async \
    system/ulib/fidl-async-2 \
    system/ulib/fidl-utils \
    system/ulib/image-format \
    system/ulib/region-alloc \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/driver \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-platform-bus \
    system/banjo/ddk-protocol-platform-device \
    system/banjo/ddk-protocol-platform-proxy \
    system/banjo/ddk-protocol-sysmem \

MODULE_COMPILEFLAGS += \
    -Isystem/ulib/fit/include \

include make/module.mk

#
# Build sysmem proxy client driver.
#

MODULE := $(LOCAL_DIR).proxy

MODULE_TYPE := driver

MODULE_SRCS += \
    $(LOCAL_DIR)/sysmem-proxy-client.c \

MODULE_STATIC_LIBS := system/ulib/ddk

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-sysmem \
    system/banjo/ddk-protocol-platform-bus \
    system/banjo/ddk-protocol-platform-device \
    system/banjo/ddk-protocol-platform-proxy \

include make/module.mk
