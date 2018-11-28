# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

# Common Code

LOCAL_SRCS := \
    $(LOCAL_DIR)/system-topology.cpp \

# system-topology

MODULE := $(LOCAL_DIR)

MODULE_GROUP := core

MODULE_SRCS := $(LOCAL_SRCS)

MODULE_DEPS := \
    kernel/lib/fbl \

MODULE_NAME := system-topology

include make/module.mk


# system-topology_test

MODULE := $(LOCAL_DIR).test

MODULE_TYPE := hosttest

MODULE_SRCS := $(LOCAL_SRCS) $(LOCAL_DIR)/system-topology_test.cpp

MODULE_COMPILEFLAGS := \
        -Isystem/ulib/fbl/include \
        -Isystem/ulib/runtests-utils/include \
        -Isystem/ulib/unittest/include \

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \
    system/ulib/pretty.hostlib \
    system/ulib/runtests-utils.hostlib \
    system/ulib/unittest.hostlib \

MODULE_NAME := system-topology_test

MODULE_DEFINES += BUILD_FOR_TEST=1

include make/module.mk

