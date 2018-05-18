# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/runtests-utils-test.cpp \
    $(LOCAL_DIR)/log-exporter-test.cpp \

# We have to include this from runtests-utils because transitive dependencies don't
# get linked in automatically.
MODULE_FIDL_LIBS := \
    system/fidl/logger

MODULE_STATIC_LIBS := \
    system/ulib/async \
    system/ulib/async-loop \
    system/ulib/async-loop.cpp \
    system/ulib/async.cpp \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/fs \
    system/ulib/memfs \
    system/ulib/runtests-utils \
    system/ulib/sync \
    system/ulib/trace \
    system/ulib/zx \
    system/ulib/zxcpp \

# We have to include all MODULE_LIBS from runtests-utils because transitive dependencies don't
# get linked in automatically.
MODULE_LIBS := \
    system/ulib/async.default \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/launchpad \
    system/ulib/trace-engine \
    system/ulib/unittest \
    system/ulib/zircon \

include make/module.mk
