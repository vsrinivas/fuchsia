# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_NAME := bootsvc
MODULE_TYPE := userapp
MODULE_GROUP := core

MODULE_SRCS += \
    $(LOCAL_DIR)/bootfs-loader-service.cpp \
    $(LOCAL_DIR)/bootfs-service.cpp \
    $(LOCAL_DIR)/main.cpp \
    $(LOCAL_DIR)/util.cpp \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-io \

MODULE_HEADER_DEPS := \
    system/ulib/zircon-internal

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/bootdata \
    system/ulib/bootfs \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/fs \
    system/ulib/loader-service \
    system/ulib/memfs \
    system/ulib/memfs.cpp \
    system/ulib/trace \
    system/ulib/zx \
    system/ulib/zxcpp \
    third_party/ulib/lz4 \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/launchpad \
    system/ulib/trace-engine \
    system/ulib/zircon \

include make/module.mk

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := userapp
MODULE_GROUP := test

MODULE_SRCS := \
    $(LOCAL_DIR)/integration-test.cpp \
    $(LOCAL_DIR)/util.cpp \

MODULE_NAME := bootsvc-tests

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk
