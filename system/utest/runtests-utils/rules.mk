# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

#
# Userspace tests
#

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/fuchsia-run-test.cpp \
    $(LOCAL_DIR)/fuchsia-test-main.cpp \
    $(LOCAL_DIR)/fuchsia-run-test.cpp \
    $(LOCAL_DIR)/runtests-utils-test.cpp \
    $(LOCAL_DIR)/runtests-utils-test-utils.cpp \
    $(LOCAL_DIR)/log-exporter-test.cpp \

# We have to include this from runtests-utils because transitive dependencies don't
# get linked in automatically.
MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-logger

MODULE_HEADER_DEPS := \
    system/ulib/zircon-internal \

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/async.cpp \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/loader-service \
    system/ulib/runtests-utils \
    system/ulib/sync \
    system/ulib/zx \
    system/ulib/zxcpp \

# We have to include all MODULE_LIBS from runtests-utils because transitive dependencies don't
# get linked in automatically.
MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/memfs \
    system/ulib/launchpad \
    system/ulib/trace-engine \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk


#
# Host tests
#

MODULE := $(LOCAL_DIR).hostapp

MODULE_TYPE := hosttest

MODULE_NAME := runtests-utils-test

MODULE_SRCS := \
    $(LOCAL_DIR)/posix-test-main.cpp \
    $(LOCAL_DIR)/runtests-utils-test.cpp \
    $(LOCAL_DIR)/runtests-utils-test-utils.cpp \

MODULE_COMPILEFLAGS := \
    -Isystem/ulib/fbl/include \
    -Isystem/ulib/runtests-utils/include \
    -Isystem/ulib/unittest/include \

MODULE_HEADER_DEPS := \
    system/ulib/zircon-internal \

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \
    system/ulib/pretty.hostlib \
    system/ulib/runtests-utils.hostlib \
    system/ulib/unittest.hostlib \

include make/module.mk

include $(LOCAL_DIR)/helper/rules.mk
