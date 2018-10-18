# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

DEVMGR_SRCS := system/core/devmgr

MODULE := $(LOCAL_DIR)

MODULE_NAME := driver

MODULE_TYPE := userlib

MODULE_EXPORT := so
MODULE_SO_NAME := driver

MODULE_COMPILEFLAGS := -fvisibility=hidden

MODULE_SRCS := \
	$(DEVMGR_SRCS)/devhost.cpp \
	$(DEVMGR_SRCS)/devhost-api.cpp \
	$(DEVMGR_SRCS)/devhost-core.cpp \
	$(DEVMGR_SRCS)/devhost-rpc-server.cpp \
	$(DEVMGR_SRCS)/devhost-shared.cpp \

ifeq ($(call TOBOOL,$(ENABLE_DRIVER_TRACING)),true)
MODULE_SRCS += \
    $(DEVMGR_SRCS)/devhost-tracing.cpp
MODULE_HEADER_DEPS := \
    system/ulib/trace-provider
endif

MODULE_FIDL_LIBS := \
    system/fidl/fuchsia-io \

MODULE_STATIC_LIBS := \
    system/ulib/ddk \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/sync \
    system/ulib/port \
    system/ulib/zx \
    system/ulib/zxcpp \

# There are pieces of the trace engine that are always present.
# They don't provide tracing support, but the tracing API provides
# them even if #define NTRACE.
MODULE_STATIC_LIBS += \
    system/ulib/trace-engine.driver

ifeq ($(call TOBOOL,$(ENABLE_DRIVER_TRACING)),true)

MODULE_STATIC_LIBS += \
    system/ulib/async \
    system/ulib/async.cpp \
    system/ulib/async.default \
    system/ulib/async-loop \
    system/ulib/trace.driver \
    system/ulib/trace-provider

# Since the tracing support is brought in via an archive, we need explicit
# references to ensure everything is present.
MODULE_EXTRA_OBJS := system/ulib/trace-engine/ddk-exports.ld

else

# Some symbols still need to be present even if tracing is disabled.
# See the linker script for details.
MODULE_EXTRA_OBJS := system/ulib/trace-engine/ddk-disabled-exports.ld

endif

MODULE_LIBS := system/ulib/fdio system/ulib/zircon system/ulib/c

include make/module.mk
