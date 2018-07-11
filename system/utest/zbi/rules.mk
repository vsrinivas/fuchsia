# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

LOCAL_SRCS := \
    $(LOCAL_DIR)/zbi.cpp

MODULE_SRCS += $(LOCAL_SRCS)

MODULE_NAME := zbi-test

MODULE_LIBS := system/ulib/unittest system/ulib/fdio system/ulib/c

MODULE_STATIC_LIBS += system/ulib/libzbi system/ulib/fbl

include make/module.mk

# Host version of the test.

MODULE := $(LOCAL_DIR).hosttest

MODULE_TYPE := hosttest

MODULE_NAME := zbi-test

MODULE_SRCS += $(LOCAL_SRCS)

MODULE_COMPILEFLAGS += -Wno-address-of-packed-member

MODULE_COMPILEFLAGS += \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/libzbi/include \
    -Isystem/ulib/unittest/include \

MODULE_HOST_LIBS += \
    system/ulib/fbl.hostlib \
    system/ulib/libzbi.hostlib \
    system/ulib/pretty.hostlib \
    system/ulib/unittest.hostlib \

include make/module.mk
