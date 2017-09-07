# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE_SRCS += \
    $(LOCAL_DIR)/collector.cpp \
    $(LOCAL_DIR)/collector_unittest.cpp \
    $(LOCAL_DIR)/hw_rng_collector.cpp \
    $(LOCAL_DIR)/jitterentropy_collector.cpp \
    $(LOCAL_DIR)/quality_test.cpp

MODULE_DEPS += \
    kernel/dev/hw_rng \
    kernel/lib/fbl \
    kernel/lib/unittest \
    third_party/lib/jitterentropy
