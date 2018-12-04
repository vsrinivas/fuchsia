# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

fit_srcs := \
    $(LOCAL_DIR)/promise.cpp \
    $(LOCAL_DIR)/scheduler.cpp \
    $(LOCAL_DIR)/sequencer.cpp \
    $(LOCAL_DIR)/single_threaded_executor.cpp \

#
# Userspace library.
#

# Disabled for now because libstdc++ isn't available for Zircon targets yet.
# We need the target to exist for the SDK to pick it up though.
MODULE_COMPILEFLAGS += -fvisibility=hidden \
    -DFIT_NO_STD_FOR_ZIRCON_USERSPACE

MODULE := $(LOCAL_DIR)
MODULE_TYPE := userlib
MODULE_PACKAGE := src
MODULE_SRCS := $(fit_srcs)

include make/module.mk

#
# Host library.
#

MODULE := $(LOCAL_DIR).hostlib
MODULE_TYPE := hostlib
MODULE_SRCS := $(fit_srcs)

include make/module.mk

# Clear local variables.

fit_srcs :=
