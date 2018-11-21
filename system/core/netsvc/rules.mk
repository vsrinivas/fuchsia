# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_NAME := netsvc

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/debuglog.c \
    $(LOCAL_DIR)/device_id.c \
    $(LOCAL_DIR)/netboot.c \
    $(LOCAL_DIR)/netfile.c \
    $(LOCAL_DIR)/netsvc.c \
    $(LOCAL_DIR)/tftp.c \
    $(LOCAL_DIR)/zbi.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/inet6 \
    system/ulib/libzbi \
    system/ulib/sync \
    system/ulib/tftp \
    system/ulib/zx \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-device-manager \
    system/fidl/zircon-ethernet \

include make/module.mk
