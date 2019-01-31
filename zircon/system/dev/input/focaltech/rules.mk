# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/focaltech.c \
    $(LOCAL_DIR)/ft_device.cpp \

MODULE_STATIC_LIBS := \
    system/dev/lib/focaltech \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/hid \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/sync \
    system/ulib/hid

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-gpio \
    system/banjo/ddk-protocol-hidbus \
    system/banjo/ddk-protocol-i2c \
    system/banjo/ddk-protocol-platform-device \
    system/banjo/ddk-protocol-test \

include make/module.mk
