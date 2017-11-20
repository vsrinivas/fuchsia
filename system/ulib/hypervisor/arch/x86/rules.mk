# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ARCH_DIR := $(GET_LOCAL_DIR)

MODULE_SRCS += \
    $(ARCH_DIR)/acpi.cpp \
    $(ARCH_DIR)/decode.cpp \
    $(ARCH_DIR)/io_apic.cpp \
    $(ARCH_DIR)/io_port.cpp \
    $(ARCH_DIR)/local_apic.cpp \

MODULE_STATIC_LIBS += \
    third_party/ulib/acpica \

MODULE_CPPFLAGS += \
    -Ithird_party/lib/acpica/source/include \
