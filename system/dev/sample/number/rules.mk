# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_NAME := demo-number

MODULE_GROUP := ddk-sample

MODULE_TYPE := driver

MODULE_SRCS := $(LOCAL_DIR)/demo-number.c

MODULE_FIDL_LIBS := system/fidl/zircon-sample

MODULE_STATIC_LIBS := system/ulib/ddk system/ulib/fidl

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

include make/module.mk
