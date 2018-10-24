// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define T931_GPIO_BASE                  0xff634400
#define T931_GPIO_LENGTH                0x400
#define T931_GPIO_A0_BASE               0xff800000
#define T931_GPIO_AO_LENGTH             0x1000
#define T931_GPIO_INTERRUPT_BASE        0xffd00000
#define T931_GPIO_INTERRUPT_LENGTH      0x10000
#define T931_I2C_AOBUS_BASE             (T931_GPIO_A0_BASE + 0x5000)
#define T931_CBUS_BASE                  0xffd00000
#define T931_CBUS_LENGTH                0x100000
#define T931_I2C0_BASE                  (T931_CBUS_BASE + 0x1f000)
#define T931_I2C1_BASE                  (T931_CBUS_BASE + 0x1e000)
#define T931_I2C2_BASE                  (T931_CBUS_BASE + 0x1d000)
#define T931_I2C3_BASE                  (T931_CBUS_BASE + 0x1c000)

#define T931_DOS_BASE                   0xff620000
#define T931_DOS_LENGTH                 0x10000

#define T931_DMC_BASE                   0xff638000
#define T931_DMC_LENGTH                 0x1000

#define T931_HIU_BASE                   0xff63c000
#define T931_HIU_LENGTH                 0x2000

#define T931_AOBUS_BASE                 0xff800000
#define T931_AOBUS_LENGTH               0x100000

#define T931_CBUS_BASE                  0xffd00000
#define T931_CBUS_LENGTH                0x100000

#define T931_MSR_CLK_BASE               0xffd18000
#define T931_MSR_CLK_LENGTH             0x1000

#define T931_MALI_BASE                  0xffe40000
#define T931_MALI_LENGTH                0x40000

// MIPI CSI & Adapter
#define T931_CSI_PHY0_BASE              0xff650000
#define T931_CSI_PHY0_LENGTH            0x2000
#define T931_APHY_BASE                  0xff63c300
#define T931_APHY_LENGTH                0x100
#define T931_CSI_HOST0_BASE             0xff654000
#define T931_CSI_HOST0_LENGTH           0x100
#define T931_MIPI_ADAPTER_BASE          0xff650000
#define T931_MIPI_ADAPTER_LENGTH        0x6000

// Power domain
#define T931_POWER_DOMAIN_BASE          0xff800000
#define T931_POWER_DOMAIN_LENGTH        0x1000

// Memory Power Domain
#define T931_MEMORY_PD_BASE             0xff63c000
#define T931_MEMORY_PD_LENGTH           0x1000

// Reset
#define T931_RESET_BASE                 0xffd01000
#define T931_RESET_LENGTH               0x100

// USB.
#define T931_USB0_BASE                  0xff500000
#define T931_USB0_LENGTH                0x100000

#define T931_USBPHY21_BASE              0xff63a000
#define T931_USBPHY21_LENGTH            0x2000

#define T931_SD_EMMC_C_BASE             0xffE07000
#define T931_SD_EMMC_C_LENGTH           0x2000

// Reset register offsets
#define T931_RESET0_REGISTER          0x04
#define T931_RESET1_REGISTER          0x08
#define T931_RESET2_REGISTER          0x0c
#define T931_RESET3_REGISTER          0x10
#define T931_RESET4_REGISTER          0x14
#define T931_RESET6_REGISTER          0x1c
#define T931_RESET7_REGISTER          0x20
#define T931_RESET0_MASK              0x40
#define T931_RESET1_MASK              0x44
#define T931_RESET2_MASK              0x48
#define T931_RESET3_MASK              0x4c
#define T931_RESET4_MASK              0x50
#define T931_RESET6_MASK              0x58
#define T931_RESET7_MASK              0x5c
#define T931_RESET0_LEVEL             0x80
#define T931_RESET1_LEVEL             0x84
#define T931_RESET2_LEVEL             0x88
#define T931_RESET3_LEVEL             0x8c
#define T931_RESET4_LEVEL             0x90
#define T931_RESET6_LEVEL             0x98
#define T931_RESET7_LEVEL             0x9c

// IRQs
#define T931_DEMUX_IRQ                  55
#define T931_PARSER_IRQ                 64
#define T931_DOS_MBOX_0_IRQ             75
#define T931_DOS_MBOX_1_IRQ             76
#define T931_DOS_MBOX_2_IRQ             77
#define T931_GPIO_IRQ_0                 96
#define T931_GPIO_IRQ_1                 97
#define T931_GPIO_IRQ_2                 98
#define T931_GPIO_IRQ_3                 99
#define T931_GPIO_IRQ_4                 100
#define T931_GPIO_IRQ_5                 101
#define T931_GPIO_IRQ_6                 102
#define T931_GPIO_IRQ_7                 103
#define T931_I2C0_IRQ                   53
#define T931_I2C1_IRQ                   246
#define T931_I2C2_IRQ                   247
#define T931_I2C3_IRQ                   71
#define T931_I2C_AO_0_IRQ               227
#define T931_USB0_IRQ                   62
#define T931_SD_EMMC_C_IRQ              223
#define T931_MALI_IRQ_GP                192
#define T931_MALI_IRQ_GPMMU             193
#define T931_MALI_IRQ_PP                194
#define T931_MIPI_ADAPTER_IRQ           211

// Alternate Functions for EMMC
#define T931_EMMC_D0                    T931_GPIOBOOT(0)
#define T931_EMMC_D0_FN                 1
#define T931_EMMC_D1                    T931_GPIOBOOT(1)
#define T931_EMMC_D1_FN                 1
#define T931_EMMC_D2                    T931_GPIOBOOT(2)
#define T931_EMMC_D2_FN                 1
#define T931_EMMC_D3                    T931_GPIOBOOT(3)
#define T931_EMMC_D3_FN                 1
#define T931_EMMC_D4                    T931_GPIOBOOT(4)
#define T931_EMMC_D4_FN                 1
#define T931_EMMC_D5                    T931_GPIOBOOT(5)
#define T931_EMMC_D5_FN                 1
#define T931_EMMC_D6                    T931_GPIOBOOT(6)
#define T931_EMMC_D6_FN                 1
#define T931_EMMC_D7                    T931_GPIOBOOT(7)
#define T931_EMMC_D7_FN                 1
#define T931_EMMC_CLK                   T931_GPIOBOOT(8)
#define T931_EMMC_CLK_FN                1
#define T931_EMMC_RST                   T931_GPIOBOOT(9)
#define T931_EMMC_RST_FN                1
#define T931_EMMC_CMD                   T931_GPIOBOOT(10)
#define T931_EMMC_CMD_FN                1
#define T931_EMMC_DS                    T931_GPIOBOOT(15)
#define T931_EMMC_DS_FN                 1
