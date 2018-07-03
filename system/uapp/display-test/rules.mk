# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp
MODULE_GROUP := misc

MODULE_SRCS += \
    $(LOCAL_DIR)/display.cpp \
    $(LOCAL_DIR)/image.cpp \
    $(LOCAL_DIR)/main.cpp \
    $(LOCAL_DIR)/virtual-layer.cpp \

MODULE_STATIC_LIBS := \
    system/ulib/fbl \
    system/ulib/fidl \
    system/ulib/zx \
    system/ulib/zxcpp \

MODULE_FIDL_LIBS := system/fidl/fuchsia-display

MODULE_LIBS := system/ulib/zircon system/ulib/fdio system/ulib/c

include make/module.mk
