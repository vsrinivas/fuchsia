# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

# Userspace library.

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS = \
    $(LOCAL_DIR)/reader.cpp \
    $(LOCAL_DIR)/records.cpp

MODULE_STATIC_LIBS := \
    system/ulib/trace-engine \
    system/ulib/mxcpp \
    system/ulib/fbl

MODULE_LIBS := \
    system/ulib/c

include make/module.mk

# Host library.

MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS = \
    $(LOCAL_DIR)/reader.cpp \
    $(LOCAL_DIR)/records.cpp

MODULE_COMPILEFLAGS := \
    -Isystem/ulib/trace-engine/include \
    -Isystem/ulib/fbl/include

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib

include make/module.mk
