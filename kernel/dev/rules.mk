# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/dev.c \
	$(LOCAL_DIR)/driver.c \
	$(LOCAL_DIR)/class/block_api.c \
	$(LOCAL_DIR)/class/i2c_api.c \
	$(LOCAL_DIR)/class/spi_api.c \
	$(LOCAL_DIR)/class/uart_api.c \
	$(LOCAL_DIR)/class/fb_api.c \
	$(LOCAL_DIR)/class/netif_api.c \

include make/module.mk
