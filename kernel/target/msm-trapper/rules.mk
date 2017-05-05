# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM := generic-arm

DEVICE_TREE := $(GET_LOCAL_DIR)/device-tree.dtb

MEMBASE := 0x80000000
# based on qca boot sequence documentation
KERNEL_LOAD_OFFSET := 0x00080000
MEMSIZE ?= 0x180000000

PERIPH_BASE_PHYS := 0x00000000U
PERIPH_BASE_VIRT := 0xffffffffc0000000UL
PERIPH_SIZE      := 0x40000000U         # 1GB
MEMORY_APERTURE_SIZE := 0x780000000UL   # (30ULL * 1024 * 1024 * 1024)

# memory to reserve to avoid stomping on bootloader data
BOOTLOADER_RESERVE_START := 0x85800000  # MSM8998_BOOT_HYP_START
BOOTLOADER_RESERVE_SIZE := 0xEF00000    # MSM8998_BOOT_APSS2_START - MSM8998_BOOT_HYP_START

# TODO(voydanoff) - move this out
MSM8998_PSHOLD_PHYS := 0x010ac000

KERNEL_DEFINES += \
	MEMBASE=$(MEMBASE) \
	MEMSIZE=$(MEMSIZE) \
	PERIPH_BASE_PHYS=$(PERIPH_BASE_PHYS) \
	PERIPH_BASE_VIRT=$(PERIPH_BASE_VIRT) \
	PERIPH_SIZE=$(PERIPH_SIZE) \
	MEMORY_APERTURE_SIZE=$(MEMORY_APERTURE_SIZE) \
	BOOTLOADER_RESERVE_START=$(BOOTLOADER_RESERVE_START) \
	BOOTLOADER_RESERVE_SIZE=$(BOOTLOADER_RESERVE_SIZE) \
	MSM8998_PSHOLD_PHYS=$(MSM8998_PSHOLD_PHYS) \
	PLATFORM_SUPPORTS_PANIC_SHELL=1 \

# extra build rules for building fastboot compatible image
include make/fastboot.mk

# build MDI
MDI_SRCS := \
    $(LOCAL_DIR)/trapper.mdi \

MDI_INCLUDES := \
    kernel/include/mdi/kernel-defs.mdi \

include make/mdi.mk
