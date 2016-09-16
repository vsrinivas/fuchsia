# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userapp

MODULE_SRCS := \
	$(LOCAL_DIR)/main.c \
    system/udev/intel-ethernet/ethernet.c \
    system/udev/intel-ethernet/ie.c

MODULE_NAME := driver-test

# if named this, this driver will be instantiated by devmgr
# to handle the intel ethernet device qemu publishes
#
#MODULE_NAME := driver-pci-8086-100e #qemu
#MODULE_NAME := driver-pci-8086-1570 #skylake nuc

MODULE_STATIC_LIBS := \
		ulib/ddk ulib/driver

MODULE_LIBS := \
    ulib/launchpad ulib/mxio ulib/magenta ulib/musl

include make/module.mk
