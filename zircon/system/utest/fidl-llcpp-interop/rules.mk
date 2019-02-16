# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

#
# fidl.test.llcpp.basic_types
#

MODULE := $(LOCAL_DIR).basictypes

MODULE_TYPE := fidl

MODULE_FIDL_LIBRARY := fidl.test.llcpp.basictypes

MODULE_SRCS += $(LOCAL_DIR)/basictypes.fidl

include make/module.mk

#
# fidl.test.llcpp.dirent
#

MODULE := $(LOCAL_DIR).dirent

MODULE_TYPE := fidl

MODULE_FIDL_LIBRARY := fidl.test.llcpp.dirent

MODULE_SRCS += $(LOCAL_DIR)/dirent.fidl

include make/module.mk

#
# fidl-llcpp-interop-test
#

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/generated/fidl_llcpp_basictypes.cpp \
    $(LOCAL_DIR)/generated/fidl_llcpp_dirent.cpp \
    $(LOCAL_DIR)/basictypes_tests.cpp \
    $(LOCAL_DIR)/dirent_tests.cpp \

MODULE_NAME := fidl-llcpp-interop-test

MODULE_FIDL_LIBS := \
    system/utest/fidl-llcpp-interop.basictypes \
    system/utest/fidl-llcpp-interop.dirent \

MODULE_STATIC_LIBS := \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async-loop.cpp \
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

MODULE_COMPILEFLAGS += \
    -Isystem/ulib/fit/include \
    -Isystem/utest/fidl-llcpp-interop/generated \

include make/module.mk
