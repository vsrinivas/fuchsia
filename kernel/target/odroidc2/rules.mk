# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM := generic-arm

MEMBASE := 0x00000000
MEMSIZE ?= 0x80000000   # 2GB
KERNEL_LOAD_OFFSET := 0x10280000

PERIPH_BASE_PHYS := 0xc0000000U
PERIPH_SIZE := 0x10200000UL
PERIPH_BASE_VIRT := 0xffffffffc0000000ULL
MEMORY_APERTURE_SIZE  := 0xc0000000UL

# TODO(hollande) this will reserve a large-ish chunk of memory
#   to protect two discontiguous regions...
#      0x00000000 - 0x01000000  bl0-bl2 code
#      0x10000000 - 0x10200000  bl3 code
#   We will need a mechanism, likely mdi, for reserving chunks
#    of memory early in boot on a per target basis.
BOOTLOADER_RESERVE_START := 0
BOOTLOADER_RESERVE_SIZE := 0x10200000

KERNEL_DEFINES += \
    MEMBASE=$(MEMBASE) \
    MEMSIZE=$(MEMSIZE) \
    PERIPH_BASE_PHYS=$(PERIPH_BASE_PHYS) \
    PERIPH_BASE_VIRT=$(PERIPH_BASE_VIRT) \
    PERIPH_SIZE=$(PERIPH_SIZE) \
    MEMORY_APERTURE_SIZE=$(MEMORY_APERTURE_SIZE) \
    BOOTLOADER_RESERVE_START=$(BOOTLOADER_RESERVE_START) \
    BOOTLOADER_RESERVE_SIZE=$(BOOTLOADER_RESERVE_SIZE) \
    PLATFORM_SUPPORTS_PANIC_SHELL=1 \

# build MDI
MDI_SRCS := $(LOCAL_DIR)/odroidc2.mdi

# extra build rules for building fastboot compatible image
include make/fastboot.mk
