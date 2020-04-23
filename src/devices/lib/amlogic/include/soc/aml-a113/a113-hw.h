// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A113_A113_HW_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A113_A113_HW_H_

#define A113_TDM_PHYS_BASE 0xff642000

// USB MMIO and IRQ
#define DWC3_MMIO_BASE 0xff500000
#define DWC3_MMIO_LENGTH 0x100000
#define DWC3_IRQ 62
#define USB_PHY_IRQ 48

// PCIe Resources
#define DW_PCIE_IRQ0 177
#define DW_PCIE_IRQ1 179

// Clock Control
#define AXG_HIU_BASE_PHYS 0xff63c000

// RAW_NAND MMIO and IRQ
#define GAUSS_RAW_NAND_REG 0xffe07800
#define GAUSS_RAW_NAND_CLKREG 0xffe07000
#define GAUSS_RAW_NAND_IRQ 66

// PWM register offsets
#define A113_PWM_AB_BASE 0xffd1b000
#define A113_PWM_AB_LENGTH 0x1000
#define A113_PWM_PWM_A 0x0
#define A113_PWM_PWM_B 0x4
#define A113_PWM_MISC_REG_AB 0x8
#define A113_DS_A_B 0xc
#define A113_PWM_TIME_AB 0x10
#define A113_PWM_A2 0x14
#define A113_PWM_B2 0x18
#define A113_PWM_BLINK_AB 0x1c

#define A113_PWM_CD_BASE 0xffd1a000
#define A113_PWM_PWM_C 0x0
#define A113_PWM_PWM_D 0x4
#define A113_PWM_MISC_REG_CD 0x8
#define A113_DS_C_D 0xc
#define A113_PWM_TIME_CD 0x10
#define A113_PWM_C2 0x14
#define A113_PWM_D2 0x18
#define A113_PWM_BLINK_CD 0x1c

#define A113_AO_PWM_AB_BASE 0xFF807000
#define A113_AO_PWM_PWM_A 0x0
#define A113_AO_PWM_PWM_B 0x4
#define A113_AO_PWM_MISC_REG_AB 0x8
#define A113_AO_DS_A_B 0xc
#define A113_AO_PWM_TIME_AB 0x10
#define A113_AO_PWM_A2 0x14
#define A113_AO_PWM_B2 0x18
#define A113_AO_PWM_BLINK_AB 0x1c

#define A113_AO_PWM_CD_BASE 0xFF802000
#define A113_AO_PWM_PWM_C 0x0
#define A113_AO_PWM_PWM_D 0x4
#define A113_AO_PWM_MISC_REG_CD 0x8
#define A113_AO_DS_C_D 0xc
#define A113_AO_PWM_TIME_CD 0x10
#define A113_AO_PWM_C2 0x14
#define A113_AO_PWM_D2 0x18
#define A113_AO_PWM_BLINK_CD 0x1c
#define A113_AO_PWM_LENGTH 0x1000

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A113_A113_HW_H_
