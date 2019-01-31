# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS := \
    $(LOCAL_DIR)/sysmem-connector.cpp \

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-sysmem

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/fbl \
    system/ulib/fzl \
    system/ulib/zx \

MODULE_LIBS := \
    system/ulib/fdio \

include make/module.mk
