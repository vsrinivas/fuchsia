// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/boot/bootdata.h>

// bootdata_kernel_driver_t types
#define KDRV_ARM_PSCI           0x49435350  // 'PSCI'
#define KDRV_ARM_GIC_V2         0x32434947  // 'GIC2'
#define KDRV_ARM_GIC_V3         0x33434947  // 'GIC3'
#define KDRV_ARM_GENERIC_TIMER  0x4D495441  // 'ATIM'
#define KDRV_PL011_UART         0x55304C50  // 'PL0U'
#define KDRV_AMLOGIC_UART       0x554C4D41  // 'AMLU'
#define KDRV_NXP_IMX_UART       0x55584D49  // 'IMXU'
#define KDRV_HISILICON_POWER    0x4F505348  // 'HSPO'
#define KDRV_AMLOGIC_HDCP       0x484C4D41  // 'AMLH'

// KDRV_ARM_PSCI parameter indices
#define KDRV_ARM_PSCI_USE_SMC                   0
#define KDRV_ARM_PSCI_SHUTDOWN_ARGS(i)          ((i) + 1)
#define KDRV_ARM_PSCI_REBOOT_ARGS(i)            ((i) + 4)
#define KDRV_ARM_PSCI_REBOOT_BOOTLOADER_ARGS(i) ((i) + 7)

// KDRV_ARM_GIC_V2 parameter indices
#define KDRV_ARM_GIC_V2_GICD_OFFSET             0
#define KDRV_ARM_GIC_V2_GICC_OFFSET             1
#define KDRV_ARM_GIC_V2_GICH_OFFSET             2
#define KDRV_ARM_GIC_V2_GICV_OFFSET             3
#define KDRV_ARM_GIC_V2_IPI_BASE                4
#define KDRV_ARM_GIC_V2_OPTIONAL                5
#define KDRV_ARM_GIC_V2_MSI_MMIO_PADDR          6

// KDRV_ARM_GIC_V3 parameter indices
#define KDRV_ARM_GIC_V3_GICD_OFFSET             0
#define KDRV_ARM_GIC_V3_GICR_OFFSET             1
#define KDRV_ARM_GIC_V3_GICR_STRIDE             2
#define KDRV_ARM_GIC_V3_IPI_BASE                3
#define KDRV_ARM_GIC_V3_OPTIONAL                4
#define KDRV_ARM_GIC_V3_MX8_GPR_PADDR           5

// KDRV_ARM_GENERIC_TIMER parameter indices
#define KDRV_ARM_GENERIC_TIMER_IRQ_PHYS         0
#define KDRV_ARM_GENERIC_TIMER_IRQ_VIRT         1
#define KDRV_ARM_GENERIC_TIMER_IRQ_SPHYS        2
#define KDRV_ARM_GENERIC_TIMER_FREQ_OVERRIDE    3

// KDRV_AMLOGIC_HDCP parameter indices
#define KDRV_AMLOGIC_HDCP_PRESET_MMIO_PADDR     0
#define KDRV_AMLOGIC_HDCP_HIU_MMIO_PADDR        1
#define KDRV_AMLOGIC_HDCP_HDMITX_MMIO_PADDR     2
