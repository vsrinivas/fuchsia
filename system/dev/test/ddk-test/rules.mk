# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := drivertest

MODULE_SRCS := \
  $(LOCAL_DIR)/ddk-test.c \
  $(LOCAL_DIR)/ddk-test-binding.c \
  $(LOCAL_DIR)/metadata-test.c \
  $(LOCAL_DIR)/usb-request-test.c \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/sync \
    system/dev/lib/usb \

MODULE_LIBS := \
    system/ulib/unittest \
    system/ulib/fdio \
    system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \
    system/dev/lib/fake-bti \

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-test \
    system/banjo/ddk-protocol-usb-request \

include make/module.mk
