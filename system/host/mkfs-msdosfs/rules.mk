# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MKMSDOS_DIR := third_party/uapp/mkfs-msdosfs

MODULE_DEFINES := _XOPEN_SOURCE _GNU_SOURCE

MODULE := $(LOCAL_DIR)

MODULE_TYPE := hostapp

MODULE_SRCS += \
	$(MKMSDOS_DIR)/mkfs_msdos.c \
	$(MKMSDOS_DIR)/mkfs_msdos.h \
	$(MKMSDOS_DIR)/newfs_msdos.c

include make/module.mk
