# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := banjo

MODULE_PACKAGE := banjo

MODULE_BANJO_LIBRARY := ddk.protocol.i2c_impl

MODULE_BANJO_NAME := i2c-impl

MODULE_SRCS += $(LOCAL_DIR)/i2c-impl.banjo

include make/module.mk

