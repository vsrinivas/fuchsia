# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ifeq ($(ENABLE_DEVHOST_V2),true)

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
	$(LOCAL_DIR)/root.c \
	system/udev/null/null.c \
	system/udev/zero/zero.c \

MODULE_STATIC_LIBS := system/ulib/ddk

MODULE_LIBS := system/ulib/driver system/ulib/mxio system/ulib/magenta system/ulib/c

MODULE_DEFINES := DEVHOST_V2=1 MAGENTA_BUILTIN_DRIVERS=1

include make/module.mk

endif
