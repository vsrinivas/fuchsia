# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM := generic-arm

DEVICE_TREE := $(GET_LOCAL_DIR)/device-tree.dtb

MEMBASE := 0x00000000

KERNEL_LOAD_OFFSET := 0x00080000
MEMSIZE ?= 0xc0000000

PERIPH_BASE_PHYS := 0xe8100000U
PERIPH_BASE_VIRT := 0xffffffffc0000000UL
PERIPH_SIZE      := 0x17f00000U
MEMORY_APERTURE_SIZE := $(MEMSIZE)

# memory to reserve to avoid stomping on bootloader data
BOOTLOADER_RESERVE_START := 0x00000000
BOOTLOADER_RESERVE_SIZE := 0x00080000

KERNEL_DEFINES += \
    PERIPH_BASE_PHYS=$(PERIPH_BASE_PHYS) \
    PERIPH_BASE_VIRT=$(PERIPH_BASE_VIRT) \
    PERIPH_SIZE=$(PERIPH_SIZE) \
    MEMORY_APERTURE_SIZE=$(MEMORY_APERTURE_SIZE) \
    BOOTLOADER_RESERVE_START=$(BOOTLOADER_RESERVE_START) \
    BOOTLOADER_RESERVE_SIZE=$(BOOTLOADER_RESERVE_SIZE) \
    PLATFORM_SUPPORTS_PANIC_SHELL=1 \

PLATFORM_VID := 2   # PDEV_VID_HI_SILICON
PLATFORM_PID := 1   # PDEV_PID_HI3660
PLATFORM_BOARD_NAME := hikey960

# build MDI
MDI_SRCS := $(LOCAL_DIR)/hikey960.mdi

# extra build rules for building kernel boot images
include make/kernel-images.mk
