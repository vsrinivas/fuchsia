# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS := $(LOCAL_DIR)/driver-info.c

include make/module.mk


MODULE := $(LOCAL_DIR).app

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_NAME := driverinfo

MODULE_SRCS := $(LOCAL_DIR)/driver-info-app.c

MODULE_STATIC_LIBS := system/ulib/driver-info

MODULE_LIBS := system/ulib/mxio system/ulib/c

include make/module.mk
