# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

# Host library.

MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS += \
    $(LOCAL_DIR)/client.cpp \

MODULE_COMPILEFLAGS := \
    -Isystem/ulib/fbl/include \

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \

include make/module.mk
