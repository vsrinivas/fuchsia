# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS += \
    $(LOCAL_DIR)/memory-probe.cpp \
    $(LOCAL_DIR)/owned-vmo-mapper.cpp \
    $(LOCAL_DIR)/pinned-vmo.cpp \
    $(LOCAL_DIR)/resizeable-vmo-mapper.cpp \
    $(LOCAL_DIR)/time.cpp \
    $(LOCAL_DIR)/vmar-manager.cpp \
    $(LOCAL_DIR)/vmo-mapper.cpp \
    $(LOCAL_DIR)/vmo-pool.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zx \

MODULE_PACKAGE := src

include make/module.mk
