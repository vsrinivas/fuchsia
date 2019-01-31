# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_NAME := netsvc

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/debuglog.cpp \
    $(LOCAL_DIR)/device_id.cpp \
    $(LOCAL_DIR)/netboot.cpp \
    $(LOCAL_DIR)/netfile.cpp \
    $(LOCAL_DIR)/netsvc.cpp \
    $(LOCAL_DIR)/tftp.cpp \
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
    system/fidl/fuchsia-hardware-ethernet \

include make/module.mk
