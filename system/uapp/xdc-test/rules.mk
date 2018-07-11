# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

# User app.

MODULE := $(LOCAL_DIR)
MODULE_TYPE := userapp
MODULE_GROUP := misc

MODULE_SRCS += \
    $(LOCAL_DIR)/xdc-device.cpp \
    $(LOCAL_DIR)/xdc-test.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/c

include make/module.mk

# Host app.

MODULE := $(LOCAL_DIR).host

MODULE_TYPE := hostapp

MODULE_SRCS += \
    $(LOCAL_DIR)/xdc-host.cpp \
    $(LOCAL_DIR)/xdc-test.cpp \

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/xdc-host-utils/include \
    -Isystem/ulib/xdc-server-utils/include \

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \
    system/ulib/xdc-host-utils.hostlib \
    system/ulib/xdc-server-utils.hostlib \

MODULE_NAME := xdc-test-host

MODULE_PACKAGE := bin

include make/module.mk
