# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

# These are the tests that don't require linkage with libc++ since
# Zircon currently only supports that for the host.
fit_zircon_friendly_usertests := \
    $(LOCAL_DIR)/function_tests.cpp \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/nullable_tests.cpp \
    $(LOCAL_DIR)/optional_tests.cpp \
    $(LOCAL_DIR)/result_tests.cpp \
    $(LOCAL_DIR)/traits_tests.cpp \

fit_tests := \
    $(fit_zircon_friendly_usertests) \
    $(LOCAL_DIR)/bridge_tests.cpp \
    $(LOCAL_DIR)/defer_tests.cpp \
    $(LOCAL_DIR)/examples/function_example1.cpp \
    $(LOCAL_DIR)/examples/function_example2.cpp \
    $(LOCAL_DIR)/examples/promise_example1.cpp \
    $(LOCAL_DIR)/examples/promise_example2.cpp \
    $(LOCAL_DIR)/examples/utils.cpp \
    $(LOCAL_DIR)/function_examples.cpp \
    $(LOCAL_DIR)/future_tests.cpp \
    $(LOCAL_DIR)/pending_task_tests.cpp \
    $(LOCAL_DIR)/promise_examples.cpp \
    $(LOCAL_DIR)/promise_tests.cpp \
    $(LOCAL_DIR)/result_examples.cpp \
    $(LOCAL_DIR)/scheduler_tests.cpp \
    $(LOCAL_DIR)/scope_tests.cpp \
    $(LOCAL_DIR)/sequencer_tests.cpp \
    $(LOCAL_DIR)/single_threaded_executor_tests.cpp \
    $(LOCAL_DIR)/suspended_task_tests.cpp \
    $(LOCAL_DIR)/variant_tests.cpp \

# Userspace tests.
# Disabled for now because libstdc++ isn't available for Zircon targets yet.
MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_NAME := fit-test

MODULE_SRCS := $(fit_zircon_friendly_usertests)

MODULE_COMPILEFLAGS += -DFIT_NO_STD_FOR_ZIRCON_USERSPACE

MODULE_STATIC_LIBS := \
    system/ulib/zxcpp \
    system/ulib/fit

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk

# Host tests.

MODULE := $(LOCAL_DIR).hostapp

MODULE_TYPE := hosttest

MODULE_NAME := fit-test

MODULE_SRCS := $(fit_tests)

MODULE_COMPILEFLAGS := \
    -Isystem/ulib/fit/include \
    -Isystem/ulib/unittest/include \

MODULE_HOST_LIBS := \
    system/ulib/fit.hostlib \
    system/ulib/pretty.hostlib \
    system/ulib/unittest.hostlib \

include make/module.mk

# Clear local variables.

fit_zircon_friendly_usertests :=
fit_tests :=
