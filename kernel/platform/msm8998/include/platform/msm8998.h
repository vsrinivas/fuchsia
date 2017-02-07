// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#define SDRAM_BASE 0x80000000

/* See memory map in doc 80-P2484-68 for for memory layout of bootloaders */
#define MSM8998_BOOT_APSS1_START                0x80000000
#define MSM8998_BOOT_HYP_START                  0x85800000
#define MSM8998_BOOT_XBL_START                  0x85e00000
#define MSM8998_BOOT_SMEM_START                 0x86000000
#define MSM8998_BOOT_TZ_STAT_START              0x86200000
#define MSM8998_BOOT_PIMEM_START                0x86300000
#define MSM8998_BOOT_PIL_REGION_START           0x8ab00000
#define MSM8998_BOOT_APSS2_START                0x94700000
#define MSM8998_BOOT_UEFI_START                 0x9fc00000

#define MSM8998_PERIPH_BASE_PHYS    (0x00000000U)
#define MSM8998_PERIPH_SIZE         (0x40000000U) // 1GB
#define MSM8998_PERIPH_BASE_VIRT    (0xffffffffc0000000UL)

#define MEMORY_APERTURE_SIZE        (30ULL * 1024 * 1024 * 1024)

#define MSM8998_GIC_BASE_PHYS       (MSM8998_PERIPH_BASE_PHYS + 0x17a00000)
#define MSM8998_GIC_BASE_VIRT       (MSM8998_PERIPH_BASE_VIRT + 0x17a00000)
#define MSM8998_PSHOLD_PHYS         (MSM8998_PERIPH_BASE_PHYS + 0x010ac000)
#define MSM8998_PSHOLD_VIRT         (MSM8998_PERIPH_BASE_VIRT + 0x010ac000)

/* interrupts */
#define PPI_BASE    16  // first per-processor interrupt
#define SPI_BASE    32  // first system peripheral interrupt

#define ARM_GENERIC_TIMER_PHYSICAL_VIRT (PPI_BASE + 3)
#define UART_INT                        (SPI_BASE + 114)

#define MAX_INT 640
