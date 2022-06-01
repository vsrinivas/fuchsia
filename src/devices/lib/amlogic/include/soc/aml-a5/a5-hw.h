// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_HW_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_HW_H_

// HIU - includes clock control registers

// gpio
#define A5_GPIO_BASE 0xfe004000
#define A5_GPIO_LENGTH 0x2000
#define A5_GPIO_INTERRUPT_BASE ((0x0020 << 2) + A5_GPIO_BASE)
#define A5_GPIO_INTERRUPT_LENGTH 0x8

// i2c

// spicc

// Peripherals - datasheet is nondescript about this section, but it contains
//  top level ethernet control and temp sensor registers

// Ethernet

// eMMC

// NNA

// Power domain

// Memory Power Domain

// Reset

// IRQs
#define A5_GPIO_IRQ_0 42   // 32+10
#define A5_GPIO_IRQ_1 43   // 32+11
#define A5_GPIO_IRQ_2 44   // 32+12
#define A5_GPIO_IRQ_3 45   // 32+13
#define A5_GPIO_IRQ_4 46   // 32+14
#define A5_GPIO_IRQ_5 47   // 32+15
#define A5_GPIO_IRQ_6 48   // 32+16
#define A5_GPIO_IRQ_7 49   // 32+17
#define A5_GPIO_IRQ_8 50   // 32+18
#define A5_GPIO_IRQ_9 51   // 32+19
#define A5_GPIO_IRQ_10 52  // 32+20
#define A5_GPIO_IRQ_11 53  // 32+21

// PWM
#define A5_PWM_LENGTH 0x2000  // applies to each PWM bank
#define A5_PWM_AB_BASE 0xfe058000
#define A5_PWM_PWM_A 0x0
#define A5_PWM_PWM_B 0x4
#define A5_PWM_MISC_REG_AB 0x8
#define A5_DS_A_B 0xc
#define A5_PWM_TIME_AB 0x10
#define A5_PWM_A2 0x14
#define A5_PWM_B2 0x18
#define A5_PWM_BLINK_AB 0x1c
#define A5_PWM_LOCK_AB 0x20

#define A5_PWM_CD_BASE 0xfe05a000
#define A5_PWM_PWM_C 0x0
#define A5_PWM_PWM_D 0x4
#define A5_PWM_MISC_REG_CD 0x8
#define A5_DS_C_D 0xc
#define A5_PWM_TIME_CD 0x10
#define A5_PWM_C2 0x14
#define A5_PWM_D2 0x18
#define A5_PWM_BLINK_CD 0x1c
#define A5_PWM_LOCK_CD 0x20

#define A5_PWM_EF_BASE 0xfe05c000
#define A5_PWM_PWM_E 0x0
#define A5_PWM_PWM_F 0x4
#define A5_PWM_MISC_REG_EF 0x8
#define A5_DS_E_F 0xc
#define A5_PWM_TIME_EF 0x10
#define A5_PWM_E2 0x14
#define A5_PWM_F2 0x18
#define A5_PWM_BLINK_EF 0x1c
#define A5_PWM_LOCK_EF 0x20

#define A5_PWM_GH_BASE 0xfe05e000
#define A5_PWM_PWM_G 0x0
#define A5_PWM_PWM_H 0x4
#define A5_PWM_MISC_REG_GH 0x8
#define A5_DS_G_H 0xc
#define A5_PWM_TIME_GH 0x10
#define A5_PWM_G2 0x14
#define A5_PWM_H2 0x18
#define A5_PWM_BLINK_GH 0x1c
#define A5_PWM_LOCK_GH 0x20

// USB

// Temperature

// These registers are used to derive calibration data for the temperature sensors. The registers
// are not documented in the datasheet - they were copied over from u-boot/Cast code.

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_HW_H_
