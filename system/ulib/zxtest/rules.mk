# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)
MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := \
    $(LOCAL_DIR)/assertion.cpp \
    $(LOCAL_DIR)/c-wrappers.cpp \
    $(LOCAL_DIR)/event-broadcaster.cpp \
    $(LOCAL_DIR)/reporter.cpp \
    $(LOCAL_DIR)/runner.cpp \
    $(LOCAL_DIR)/runner-options.cpp \
    $(LOCAL_DIR)/test-case.cpp \
    $(LOCAL_DIR)/test.cpp \
    $(LOCAL_DIR)/test-info.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/zircon \

MODULE_PACKAGE := src

include make/module.mk

#
# zxtest host lib
#
MODULE := $(LOCAL_DIR).hostlib
MODULE_TYPE := hostlib

MODULE_COMPILEFLAGS += -fvisibility=hidden

MODULE_SRCS := \
    $(LOCAL_DIR)/assertion.cpp \
    $(LOCAL_DIR)/c-wrappers.cpp \
    $(LOCAL_DIR)/event-broadcaster.cpp \
    $(LOCAL_DIR)/reporter.cpp \
    $(LOCAL_DIR)/runner.cpp \
    $(LOCAL_DIR)/runner-options.cpp \
    $(LOCAL_DIR)/test-case.cpp \
    $(LOCAL_DIR)/test.cpp \
    $(LOCAL_DIR)/test-info.cpp \

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/ulib/fbl/include \

MODULE_PACKAGE := src

include make/module.mk

#
# zxtest unit tests
#
MODULE := $(LOCAL_DIR).test

MODULE_NAME := zxtest-test

MODULE_TYPE := usertest

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS := \
    $(TEST_DIR)/event-broadcaster_test.cpp \
    $(TEST_DIR)/runner_test.cpp \
    $(TEST_DIR)/test-case_test.cpp \
    $(TEST_DIR)/test-info_test.cpp \
    $(TEST_DIR)/test_test.cpp \
    $(TEST_DIR)/main.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/zxtest \

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/c \

include make/module.mk

#
# zxtest integratione tests Sanity checks that macros are working.
#
MODULE := $(LOCAL_DIR).integration-test

MODULE_NAME := zxtest-integration-test

MODULE_TYPE := usertest

TEST_DIR := $(LOCAL_DIR)/test/integration/

MODULE_SRCS := \
    $(TEST_DIR)/helper.cpp \
    $(TEST_DIR)/register_test.c \
    $(TEST_DIR)/register_test.cpp \
    $(TEST_DIR)/main.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/zx \
    system/ulib/zxcpp \
    system/ulib/zxtest \

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/zircon \
    system/ulib/c \

include make/module.mk

#
# zxtest unit tests - host
#
MODULE := $(LOCAL_DIR).host-test

MODULE_NAME := zxtest-test

MODULE_TYPE := hosttest

TEST_DIR := $(LOCAL_DIR)/test

MODULE_SRCS := \
    $(TEST_DIR)/event-broadcaster_test.cpp \
    $(TEST_DIR)/runner_test.cpp \
    $(TEST_DIR)/test-case_test.cpp \
    $(TEST_DIR)/test-info_test.cpp \
    $(TEST_DIR)/test_test.cpp \
    $(TEST_DIR)/main.cpp \

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \
    system/ulib/zxtest.hostlib \

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/ulib/fbl/include \

include make/module.mk

#
# zxtest integratione tests Sanity checks that macros are working.
#
MODULE := $(LOCAL_DIR).host-integration-test

MODULE_NAME := zxtest-integration-test

MODULE_TYPE := hosttest

TEST_DIR := $(LOCAL_DIR)/test/integration/

MODULE_SRCS := \
    $(TEST_DIR)/helper.cpp \
    $(TEST_DIR)/register_test.c \
    $(TEST_DIR)/register_test.cpp \
    $(TEST_DIR)/main.cpp \

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \
    system/ulib/zxtest.hostlib \

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/ulib/fbl/include \

include make/module.mk

