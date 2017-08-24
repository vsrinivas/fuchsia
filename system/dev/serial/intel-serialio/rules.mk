# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ifeq ($(ARCH),x86)

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := \
    $(LOCAL_DIR)/serialio.c \
    $(LOCAL_DIR)/dma/dma.c \
    $(LOCAL_DIR)/i2c/controller.c \
    $(LOCAL_DIR)/i2c/slave.c \
    $(LOCAL_DIR)/sdio/sdio.c \
    $(LOCAL_DIR)/spi/spi.c \
    $(LOCAL_DIR)/uart/uart.c

MODULE_COMPILEFLAGS += -I $(LOCAL_DIR)/intel-serialio-include/

MODULE_STATIC_LIBS := system/ulib/ddk

MODULE_LIBS := system/ulib/driver system/ulib/magenta system/ulib/c

include make/module.mk

endif
