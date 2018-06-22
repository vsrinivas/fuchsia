# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
LOCAL_INC := $(LOCAL_DIR)/include/lib/tee-client-api

#
# libtee-client-api.so: the client library
#

MODULE := $(LOCAL_DIR)
MODULE_NAME := tee-client-api

MODULE_TYPE := userlib

MODULE_SRCS = \
    $(LOCAL_DIR)/tee-client-api.c \

MODULE_PACKAGE_SRCS := $(MODULE_SRCS)
MODULE_PACKAGE_INCS := \
    $(LOCAL_INC)/tee_client_api.h \

MODULE_SO_NAME := tee-client-api
MODULE_EXPORT := so

MODULE_LIBS := \
    system/ulib/c \

MODULE_PACKAGE := src

include make/module.mk
