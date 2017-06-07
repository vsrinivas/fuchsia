# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

################################################################################
#
# TODO(tkilbourn)
# This driver is disabled until we can update it to use the new
# MX_PROTOCOL_HIDBUS protocol, which requires rationalizing all of the device
# lifecycle events.
#
################################################################################

#LOCAL_DIR := $(GET_LOCAL_DIR)
#
#MODULE := $(LOCAL_DIR)
#
#MODULE_TYPE := driver
#
#MODULE_SRCS := $(LOCAL_DIR)/hidctl.c
#
#MODULE_STATIC_LIBS := system/ulib/ddk
#
#MODULE_LIBS := system/ulib/driver system/ulib/magenta system/ulib/c
#
#include make/module.mk

