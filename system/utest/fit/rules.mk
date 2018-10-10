# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

fit_tests := \
    $(LOCAL_DIR)/defer_tests.cpp \
    $(LOCAL_DIR)/examples/function_example1.cpp \
    $(LOCAL_DIR)/examples/function_example2.cpp \
    $(LOCAL_DIR)/function_tests.cpp \
    $(LOCAL_DIR)/function_traits_tests.cpp \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/nullable_tests.cpp \
    $(LOCAL_DIR)/optional_tests.cpp \

# Userspace tests.
# Disabled for now because libstdc++ isn't available for Zircon targets yet.
ifeq (0,1)
MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_NAME := fit-test

MODULE_SRCS := $(fit_tests)

MODULE_STATIC_LIBS := \
    system/ulib/zxcpp \
    system/ulib/fit

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk
endif

# Host tests.

MODULE := $(LOCAL_DIR).hostapp

MODULE_TYPE := hosttest

MODULE_NAME := fit-test

MODULE_SRCS := $(fit_tests)

MODULE_COMPILEFLAGS := \
    -Isystem/ulib/fit/include \
    -Isystem/ulib/unittest/include \

# Suppress warnings about self-move and self-assignment since we have
# tests that intentionally verify these behaviors.
MODULE_COMPILEFLAGS += \
    -Wno-self-move -Wno-self-assign-overloaded \

MODULE_HOST_LIBS := \
    system/ulib/fit.hostlib \
    system/ulib/pretty.hostlib \
    system/ulib/unittest.hostlib \

include make/module.mk

# Clear local variables.

fit_tests :=
