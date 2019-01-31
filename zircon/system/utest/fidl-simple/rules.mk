# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

#
# fidl.test.spaceship
#

MODULE := $(LOCAL_DIR).spaceship

MODULE_TYPE := fidl

MODULE_FIDL_LIBRARY := fidl.test.spaceship

MODULE_SRCS += $(LOCAL_DIR)/spaceship.fidl

include make/module.mk

#
# fidl.test.fakesocket
#

MODULE := $(LOCAL_DIR).fakesocket

MODULE_TYPE := fidl

MODULE_FIDL_LIBRARY := fidl.test.fakesocket

MODULE_SRCS += $(LOCAL_DIR)/fakesocket.fidl

include make/module.mk

#
# fidl-simple-test
#

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/client_tests.c \
    $(LOCAL_DIR)/fakesocket_tests.cpp \
    $(LOCAL_DIR)/ldsvc_tests.c \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/server_tests.c \
    $(LOCAL_DIR)/spaceship_tests.c \
    $(LOCAL_DIR)/spaceship_tests.cpp \

MODULE_NAME := fidl-simple-test

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-crash \
    system/fidl/fuchsia-ldsvc \
    system/fidl/fuchsia-mem \
    system/utest/fidl-simple.fakesocket \
    system/utest/fidl-simple.spaceship \

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async-loop \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/fidl-async \
    system/ulib/fidl-utils \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk
