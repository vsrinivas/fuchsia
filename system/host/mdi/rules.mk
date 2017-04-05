# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).gen

MODULE_NAME := mdigen

MODULE_TYPE := hostapp

MODULE_SRCS += \
	$(LOCAL_DIR)/mdigen.cpp \
	$(LOCAL_DIR)/node.cpp \
	$(LOCAL_DIR)/parser.cpp \
	$(LOCAL_DIR)/tokens.cpp \

include make/module.mk


MODULE := $(LOCAL_DIR).dump

MODULE_NAME := mdidump

MODULE_TYPE := hostapp

MODULE_SRCS += \
    $(LOCAL_DIR)/mdidump.cpp \

include make/module.mk
