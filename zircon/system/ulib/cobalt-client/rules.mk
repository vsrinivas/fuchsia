# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden

COMMON_SRCS := \
    $(LOCAL_DIR)/counter.cpp \
    $(LOCAL_DIR)/metric_info.cpp \
    $(LOCAL_DIR)/histogram.cpp \
    $(LOCAL_DIR)/collector.cpp \

MODULE_SRCS += \
    $(COMMON_SRCS)\
    $(LOCAL_DIR)/cobalt_logger.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/fidl-async \
    system/ulib/fzl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_LIBS := \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/zircon \

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-cobalt \
    system/fidl/fuchsia-mem \

MODULE_PACKAGE := src

include make/module.mk

# Make a hostlib for libraries that are built for host and target host.
# The target library is a dummy, replacing the logger with a dummy logger
# that does nothing. This should remove once there is a clear separation
# on host and client code in FS.

MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS += \
	$(COMMON_SRCS)\

MODULE_COMPILEFLAGS := \
	-Isystem/ulib/fbl/include \

include make/module.mk
