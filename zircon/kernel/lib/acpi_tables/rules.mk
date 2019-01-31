# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

# Common Code

LOCAL_SRCS := \
    $(LOCAL_DIR)/acpi_tables.cpp \
    $(LOCAL_DIR)/acpi_tables_test.cpp \

# system-topology

MODULE := $(LOCAL_DIR)

MODULE_GROUP := core

MODULE_SRCS := $(LOCAL_SRCS)

MODULE_DEPS := \
    kernel/lib/unittest \

MODULE_NAME := acpi_tables

include make/module.mk
