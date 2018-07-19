# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/svchost.cpp \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-crash \
    system/fidl/fuchsia-process \
    system/fidl/fuchsia-sysmem \

MODULE_STATIC_LIBS := \
    system/ulib/inspector \
    system/ulib/svc \
    system/ulib/process-launcher \
    system/ulib/sysmem \
    system/ulib/fs \
    system/core/crashanalyzer \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/async-loop.cpp \
    system/ulib/async-loop \
    system/ulib/trace \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/pretty \
    system/ulib/zxcpp \
    system/ulib/zx

MODULE_LIBS := \
    third_party/ulib/backtrace \
    third_party/ulib/ngunwind \
    system/ulib/async.default \
    system/ulib/launchpad \
    system/ulib/fdio \
    system/ulib/c \
    system/ulib/trace-engine \
    system/ulib/zircon

include make/module.mk
