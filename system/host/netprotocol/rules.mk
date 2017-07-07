# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).netruncmd

MODULE_TYPE := hostapp

MODULE_SRCS += $(LOCAL_DIR)/netruncmd.c $(LOCAL_DIR)/netprotocol.c

MODULE_NAME := netruncmd

include make/module.mk

MODULE := $(LOCAL_DIR).netcp

MODULE_TYPE := hostapp

MODULE_SRCS += $(LOCAL_DIR)/netcp.c $(LOCAL_DIR)/netprotocol.c

MODULE_HOST_LIBS := system/ulib/tftp

MODULE_NAME := netcp

include make/module.mk

MODULE := $(LOCAL_DIR).netls

MODULE_TYPE := hostapp

MODULE_SRCS += $(LOCAL_DIR)/netls.c $(LOCAL_DIR)/netprotocol.c

MODULE_NAME := netls

include make/module.mk

MODULE := $(LOCAL_DIR).netaddr

MODULE_TYPE := hostapp

MODULE_SRCS += $(LOCAL_DIR)/netaddr.c $(LOCAL_DIR)/netprotocol.c

MODULE_NAME := netaddr

include make/module.mk
