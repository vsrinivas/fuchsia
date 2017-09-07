# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
    $(LOCAL_DIR)/global_prng.cpp \
    $(LOCAL_DIR)/global_prng_unittest.cpp \
    $(LOCAL_DIR)/hash.cpp \
    $(LOCAL_DIR)/hash_unittest.cpp \
    $(LOCAL_DIR)/prng.cpp \
    $(LOCAL_DIR)/prng_unittest.cpp

MODULE_DEPS += third_party/lib/uboringssl
MODULE_DEPS += third_party/lib/cryptolib
MODULE_DEPS += kernel/lib/explicit-memory
MODULE_DEPS += kernel/lib/fbl
MODULE_DEPS += kernel/lib/unittest

include $(LOCAL_DIR)/entropy/rules.mk

include make/module.mk
