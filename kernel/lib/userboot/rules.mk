# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS := \
    $(LOCAL_DIR)/userboot.cpp

MODULE_DEPS := \
    lib/elf

ifeq ($(call TOBOOL,$(EMBED_USER_BOOTFS)),true)
MODULE_SRCDEPS += $(USER_BOOTFS)
MODULE_DEFINES += EMBED_USER_BOOTFS=1
MODULE_COMPILEFLAGS += -DUSER_BOOTFS_FILENAME="\"$(USER_BOOTFS)\""
MODULE_SRCS += $(LOCAL_DIR)/bootfs.S
endif

include make/module.mk
