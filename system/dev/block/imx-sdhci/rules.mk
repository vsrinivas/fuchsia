# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/imx-sdhci.c

MODULE_STATIC_LIBS := system/ulib/ddk \
    system/ulib/sync \
    system/ulib/pretty \
    system/ulib/trace-provider \
    system/ulib/trace \
    system/ulib/async.cpp \
    system/ulib/async \
    system/ulib/async-loop.cpp \
    system/ulib/async-loop \
    system/ulib/zxcpp \
    system/ulib/fbl \
    system/ulib/zx

MODULE_LIBS := system/ulib/driver \
    system/ulib/zircon \
    system/ulib/c \
    system/ulib/fdio \
    system/ulib/async.default \
    system/ulib/trace-engine

MODULE_HEADER_DEPS := system/dev/lib/imx8m

include make/module.mk
