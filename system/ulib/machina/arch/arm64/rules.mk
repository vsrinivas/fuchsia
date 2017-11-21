# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ARCH_DIR := $(GET_LOCAL_DIR)

MODULE_SRCS += \
    $(ARCH_DIR)/gic_distributor.cpp \
    $(ARCH_DIR)/pl011.cpp \
