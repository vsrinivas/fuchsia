# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS = \
    $(LOCAL_DIR)/handler_impl.cpp \
    $(LOCAL_DIR)/provider_impl.cpp \
    $(LOCAL_DIR)/trace_provider.fidl.cpp \
    $(LOCAL_DIR)/trace_provider.fidl.h \

MODULE_STATIC_LIBS := \
    system/ulib/trace \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/zx \
    system/ulib/fbl \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon \
    system/ulib/fdio \
    system/ulib/trace-engine \
    system/ulib/fidl \

MODULE_PACKAGE := src

include make/module.mk
