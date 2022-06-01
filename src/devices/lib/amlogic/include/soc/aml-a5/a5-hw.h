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

// USB

// Temperature

// These registers are used to derive calibration data for the temperature sensors. The registers
// are not documented in the datasheet - they were copied over from u-boot/Cast code.

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_HW_H_
