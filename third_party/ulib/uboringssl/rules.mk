# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)
SRC_DIR := $(LOCAL_DIR)/crypto
ASM_DIR := $(LOCAL_DIR)/asm

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS := \
    $(SRC_DIR)/cpu-aarch64-magenta.cpp \

MODULE_SRCS += \
    $(SRC_DIR)/chacha/chacha.c \
    $(SRC_DIR)/fipsmodule/sha/sha256.c \
    $(SRC_DIR)/fipsmodule/sha/sha512.c \
    $(SRC_DIR)/cpu-intel.c \
    $(SRC_DIR)/crypto.c \
    $(SRC_DIR)/mem.c \

ifeq ($(ARCH),arm64)
MODULE_SRCS += \
    $(ASM_DIR)/chacha-arm64.S \
    $(ASM_DIR)/sha256-arm64.S \
    $(ASM_DIR)/sha512-arm64.S
else ifeq ($(ARCH),x86)
MODULE_SRCS += \
    $(ASM_DIR)/chacha-x86-64.S \
    $(ASM_DIR)/sha256-x86-64.S \
    $(ASM_DIR)/sha512-x86-64.S
endif

MODULE_NAME := uboringssl

MODULE_LIBS := \
    system/ulib/mxio \
    system/ulib/c \

include make/module.mk
